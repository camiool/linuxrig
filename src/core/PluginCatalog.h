#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>

// Lista de plugins disponibles, con escaneo fuera de proceso.
//
// Cada bundle se escanea en un proceso hijo (este mismo ejecutable con
// --scan-plugin): si el plugin se cuelga o se cae, solo muere el hijo y el
// bundle pasa a la lista negra. El resultado se guarda en un archivo de caché
// y solo se reescanean los bundles nuevos o modificados.
class PluginCatalog final : private juce::Thread
{
public:
    explicit PluginCatalog (juce::File cacheFile);
    ~PluginCatalog() override;

    // --- Hilo de mensajes ---------------------------------------------------
    // full = olvidar la caché y la lista negra y escanear todo de nuevo.
    void startScan (bool full);
    bool isScanning() const noexcept { return isThreadRunning(); }
    juce::String getStatus() const;

    // Tipos conocidos, ordenados por nombre.
    juce::Array<juce::PluginDescription> getPlugins() const;
    int getNumBlacklisted() const;

    // Busca por identificador; si no está en la caché, escanea el archivo en
    // este proceso (p. ej. un preset que apunta a un plugin aún sin escanear).
    std::optional<juce::PluginDescription> find (const juce::String& identifier, const juce::String& file);

    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription&,
                                                               double sampleRate, int blockSize,
                                                               juce::String& error);

    // --- Proceso hijo -------------------------------------------------------
    static bool isScannerCommandLine (const juce::StringArray& args);
    static int runScanner (const juce::StringArray& args);

private:
    enum class ScanResult { ok, failed, cancelled };

    void run() override;
    ScanResult scanInChildProcess (const juce::String& fileOrIdentifier);
    void setStatus (const juce::String&);
    void save() const;

    const juce::File cacheFile;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList known;   // tiene su propio lock: se usa desde ambos hilos

    std::atomic<bool> fullRescan { false };
    std::atomic<int> numBlacklisted { 0 };
    juce::CriticalSection statusLock;
    juce::String status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginCatalog)
};
