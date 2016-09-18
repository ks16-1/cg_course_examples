#include "Window.h"
#include "AbstractWindowClient.h"
#include "libchapter3_private.h"
#include <SDL2/SDL.h>

namespace
{
std::once_flag g_glewInitOnceFlag;

struct SDLWindowDeleter
{
    void operator()(SDL_Window *ptr)
    {
        SDL_DestroyWindow(ptr);
    }
};
// Используем unique_ptr с явно заданным функтором удаления вместо delete.
using SDLWindowPtr = std::unique_ptr<SDL_Window, SDLWindowDeleter>;

struct SDLGLContextDeleter
{
    void operator()(SDL_GLContext ptr)
    {
        SDL_GL_DeleteContext(ptr);
    }
};
// Используем unique_ptr с явно заданным функтором удаления вместо delete.
using SDLGLContextPtr = std::unique_ptr<void, SDLGLContextDeleter>;

class CChronometer
{
public:
    CChronometer()
        : m_lastTime(std::chrono::system_clock::now())
    {
    }

    float GrabDeltaTime()
    {
        auto newTime = std::chrono::system_clock::now();
        auto timePassed = std::chrono::duration_cast<std::chrono::milliseconds>(newTime - m_lastTime);
        m_lastTime = newTime;
        return 0.001f * float(timePassed.count());
    }

private:
    std::chrono::system_clock::time_point m_lastTime;
};

glm::vec2 GetMousePosition(const SDL_MouseButtonEvent &event)
{
    return { event.x, event.y };
}

glm::vec2 GetMousePosition(const SDL_MouseMotionEvent &event)
{
    return { event.x, event.y };
}

void DispatchEvent(const SDL_Event &event, IWindowClient &acceptor)
{
    switch (event.type)
    {
    case SDL_KEYDOWN:
        acceptor.OnKeyDown(event.key);
        break;
    case SDL_KEYUP:
        acceptor.OnKeyUp(event.key);
        break;
    case SDL_MOUSEBUTTONDOWN:
        acceptor.OnDragBegin(GetMousePosition(event.button));
        break;
    case SDL_MOUSEBUTTONUP:
        acceptor.OnDragEnd(GetMousePosition(event.button));
        break;
    case SDL_MOUSEMOTION:
        acceptor.OnDragMotion(GetMousePosition(event.motion));
        break;
    }
}
}

class CWindow::Impl
{
public:
    void SetCoreProfileEnabled(bool enabled)
    {
        if (m_pWindow)
        {
            throw std::logic_error("Cannot change OpenGL profile after window created");
        }
        m_isCoreProfileEnabled = enabled;
    }

    void Show(const std::string &title, const glm::ivec2 &size)
    {
        m_size = size;

        unsigned initMask = SDL_INIT_VIDEO | SDL_INIT_EVENTS;
        if (SDL_WasInit(initMask) != initMask)
        {
            SDL_Init(initMask);
        }

        // Выбираем между профилями Core и Compatibility
//        const SDL_GLprofile profile = m_isCoreProfileEnabled
//                ? SDL_GL_CONTEXT_PROFILE_CORE
//                : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

        // Специальное значение SDL_WINDOWPOS_CENTERED вместо x и y заставит SDL2
        // разместить окно в центре монитора по осям x и y.
        // Для использования OpenGL вы ДОЛЖНЫ указать флаг SDL_WINDOW_OPENGL.
        m_pWindow.reset(SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                         size.x, size.y, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE));
        if (!m_pWindow)
        {
            const std::string reason = SDL_GetError();
            throw std::runtime_error("Cannot create window: " + reason);
        }

        // Создаём контекст OpenGL, связанный с окном.
        m_pGLContext.reset(SDL_GL_CreateContext(m_pWindow.get()));
        if (!m_pGLContext)
        {
            const std::string reason = SDL_GetError();
            throw std::runtime_error("SDL2 Error: " + reason);
        }

        InitGlewOnce();
    }

    glm::ivec2 GetWindowSize() const
    {
        return m_size;
    }

    void SetClient(IWindowClient *pClient)
    {
        m_pClient = pClient;
    }

    void DoMainLoop()
    {
        SDL_Event event;
        CChronometer chronometer;
        while (true)
        {
            while (SDL_PollEvent(&event) != 0)
            {
                if (!ConsumeEvent(event) && m_pClient)
                {
                    DispatchEvent(event, *m_pClient);
                }
            }
            if (m_isTerminated)
            {
                break;
            }
            // Очистка буфера кадра, обновление и рисование сцены, вывод буфера кадра.
            Clear();
            if (m_pClient)
            {
                const float deltaSeconds = chronometer.GrabDeltaTime();
                m_pClient->OnUpdateWindow(deltaSeconds);
            }
            ThrowOnGLError();
            SwapBuffers();
        }
    }

    void SetBackgroundColor(const glm::vec4 &color)
    {
        m_clearColor = color;
    }

    void Clear()const
    {
        // Заливка кадра цветом фона средствами OpenGL
        glClearColor(m_clearColor.x, m_clearColor.y, m_clearColor.z, m_clearColor.w);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void SwapBuffers()const
    {
        // Вывод нарисованного кадра в окно на экране.
        // При этом система отдаёт старый буфер для рисования нового кадра.
        // Обмен двух буферов вместо создания новых позволяет не тратить ресурсы впустую.
        SDL_GL_SwapWindow(m_pWindow.get());
    }

    bool ConsumeEvent(const SDL_Event &event)
    {
        bool consumed = false;
        if (event.type == SDL_QUIT)
        {
            m_isTerminated = true;
            consumed = true;
        }
        else if (event.type == SDL_WINDOWEVENT)
        {
            OnWindowEvent(event.window);
            consumed = true;
        }
        return consumed;
    }

private:
    void ThrowOnGLError()
    {
        GLenum error = glGetError();
        if (error != GL_NO_ERROR)
        {
            std::string message;
            switch (error)
            {
            case GL_INVALID_ENUM:
                message = "invalid enum passed to GL function (GL_INVALID_ENUM)";
                break;
            case GL_INVALID_VALUE:
                message = "invalid parameter passed to GL function (GL_INVALID_VALUE)";
                break;
            case GL_INVALID_OPERATION:
                message = "cannot execute some of GL functions in current state (GL_INVALID_OPERATION)";
                break;
            case GL_STACK_OVERFLOW:
                message = "matrix stack overflow occured inside GL (GL_STACK_OVERFLOW)";
                break;
            case GL_STACK_UNDERFLOW:
                message = "matrix stack underflow occured inside GL (GL_STACK_UNDERFLOW)";
                break;
            case GL_OUT_OF_MEMORY:
                message = "no enough memory to execute GL function (GL_OUT_OF_MEMORY)";
                break;
            default:
                message = "error in some GL extension (framebuffers, shaders, etc)";
                break;
            }
            throw std::runtime_error("OpenGL error: " + message);
        }
    }

    void OnWindowEvent(const SDL_WindowEvent &event)
    {
        if (event.event == SDL_WINDOWEVENT_RESIZED)
        {
            m_size = {event.data1, event.data2};
        }
    }

    void InitGlewOnce()
    {
        // Вызываем инициализацию GLEW только один раз за время работы приложения.
        std::call_once(g_glewInitOnceFlag, []() {
            glewExperimental = GL_TRUE;
            GLenum status = glewInit();
            if (status != GLEW_OK)
            {
                std::string reason = reinterpret_cast<const char *>(glewGetErrorString(status));
                throw std::runtime_error("GLEW initialization failed: " + reason);
            }
        });
    }

    IWindowClient *m_pClient = nullptr;
    SDLWindowPtr m_pWindow;
    SDLGLContextPtr m_pGLContext;
    glm::ivec2 m_size;
    glm::vec4 m_clearColor;
    bool m_isTerminated = false;
    bool m_isCoreProfileEnabled = false;
};

CWindow::CWindow()
    : m_pImpl(new Impl)
{
}

CWindow::~CWindow()
{
}

void CWindow::SetCoreProfileEnabled(bool enabled)
{
    m_pImpl->SetCoreProfileEnabled(enabled);
}

void CWindow::Show(const std::string &title, const glm::ivec2 &size)
{
    m_pImpl->Show(title, size);
}

void CWindow::SetBackgroundColor(const glm::vec4 &color)
{
    m_pImpl->SetBackgroundColor(color);
}

void CWindow::SetClient(IWindowClient *pClient)
{
    m_pImpl->SetClient(pClient);
}

glm::ivec2 CWindow::GetWindowSize() const
{
    return m_pImpl->GetWindowSize();
}

void CWindow::DoMainLoop()
{
    m_pImpl->DoMainLoop();
}
