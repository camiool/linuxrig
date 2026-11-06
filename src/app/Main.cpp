#include "MainComponent.h"

#include <csignal>

namespace
{
    // SIGTERM (apagado, cierre de sesión) o SIGINT (Ctrl-C): cerrar de forma
    // ordenada para que la sesión se guarde. El handler solo marca la petición.
    std::atomic<bool> quitRequested { false };

    extern "C" void requestQuit (int) { quitRequested = true; }
}

class LinuxRigApplication final : public juce::JUCEApplication,
                                  private juce::Timer
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    // El escáner de plugins es este mismo ejecutable lanzado como proceso hijo.
    bool moreThanOneInstanceAllowed() override { return isScanner(); }

    void initialise (const juce::String&) override
    {
        if (isScanner())
        {
            setApplicationReturnValue (PluginCatalog::runScanner (getCommandLineParameterArray()));
            quit();
            return;
        }

        mainWindow = std::make_unique<MainWindow> (getApplicationName());

        std::signal (SIGTERM, requestQuit);
        std::signal (SIGINT, requestQuit);
        startTimer (200);
    }

    void shutdown() override        { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }

private:
    void timerCallback() override
    {
        if (quitRequested)
        {
            stopTimer();
            systemRequestedQuit();
        }
    }

    static bool isScanner() { return PluginCatalog::isScannerCommandLine (getCommandLineParameterArray()); }

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colours::darkgrey, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (LinuxRigApplication)
