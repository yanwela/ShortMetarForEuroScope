#pragma once

#include <windows.h>
#include "EuroScopePlugIn.h"
#include <string>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <vector>

class CMyRadarScreen : public EuroScopePlugIn::CRadarScreen
{
public:
    virtual void OnAsrContentLoaded(bool Loaded) override;
    virtual void OnRefresh(HDC hDC, int Phase) override;
    virtual void OnClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button) override;
    virtual void OnMoveScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, bool Released) override;
    inline virtual void OnAsrContentToBeClosed(void) override { delete this; }
private:
    int m_GrabDX = 0, m_GrabDY = 0;
};

class CShortMetar : public EuroScopePlugIn::CPlugIn
{
public:
    enum class FilterMode { All, Used, SingleIcao, Online };
    enum class Source { Vatsim, Rasat };
    enum class Language { English, Turkish };

private:
    struct MetarData { std::string ICAO, Wind, QNH, RawOutput; int alert = 0; }; // 0=yok 1=QNH 2=tum

    std::map<std::string, MetarData> m_MetarData;
    std::set<std::string>            m_DepAirports;
    std::set<std::string>            m_ArrAirports;
    std::set<std::string>            m_OnlineAirports;   // aktif online/sct filtresinin kapsadigi meydanlar
    std::map<std::string, std::string>    m_PositionIdentifier; // .ese [POSITIONS]: CALLSIGN -> kisa kimlik (orn. ANK_W78_CTR -> W78)
    std::map<std::string, std::set<std::string>> m_SectorCoverage; // kisa kimlik -> o kimligin OWNER zincirinde gectigi tum SECTOR bloklarinin DEPAPT/ARRAPT meydanlari
    std::map<std::string, std::string>    m_PositionAirportHint; // kisa kimlik -> [POSITIONS] PREFIX alani (FMP/ATIS gibi sektor sahiplenmeyen pozisyonlar icin tek meydan fallback'i)
    bool         m_SectorDataLoaded = false;
    bool         m_OnlineIsOwn = false;   // true: kendi CID'im/pozisyonuma gore, false: .sm online sct <ad> ile sabit kimlik
    std::string  m_OnlineIdentifier;      // aktif online filtresinin kisa kimligi (sadece own=false icin sabit kalir)
    std::string m_CachedRasatBuild;   // kesfedilen build id onbellegi (her fetch'te yeniden kesfetmeyi onler)
    std::map<std::string, DWORD>     m_NewTraffic;   // yeni gelen meydan -> GetTickCount (6sn mavi)
    std::mutex   m_DataMutex;
    bool         m_IsFetching;      // metar cekimi
    bool         m_FetchTraffic;    // trafik cekimi
    DWORD        m_MetarFetchStartTick = 0;
    DWORD        m_TrafficFetchStartTick = 0;
    FilterMode   m_FilterMode;
    std::string  m_FilterIcao;        // (kullanilmiyor, geriye uyum)
    std::set<std::string> m_FilterIcaos;
    int          m_PanelX, m_PanelY;
    Source       m_Source;
    Language     m_Language = Language::English;
    bool         m_Collapsed;
    int          m_FontSize;
    bool         m_DebugMode = false;   // acikken her basarili "RASAT/VATSIM updated" mesaji chat'e basilir (varsayilan: kapali, sessiz)

    const int PANEL_OBJ_ID = 9001;

    std::string HttpGet(const wchar_t* host, const wchar_t* path, bool https = true);
    void HttpGetMulti(const wchar_t* host, const std::vector<std::wstring>& paths, std::vector<std::string>& outResults, size_t startIdx, bool https = true);
    std::string FetchRasatBuild();
    void FetchMetarAsync();
    void FetchTrafficAsync();
    void ParseVatsimData(const std::string& json);
    void ParseBulkMetar(const std::string& raw);
    void ParseRasat(const std::string& raw);
    void ShowHelp();
    void StartMetarFetch();
    void StartTrafficFetch();
    void LoadFont();
    std::string PluginDir();
    void LoadState();
    void LoadSectorData();
    void RecomputeOnlineFilter();
    std::set<std::string> ResolveSectorAirports(const std::string& identifier) const;

public:
    CShortMetar();
    virtual ~CShortMetar();

    virtual bool OnCompileCommand(const char* sCommandLine) override;
    virtual void OnTimer(int Counter) override;
    virtual EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(
        const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced,
        bool CanBeSaved, bool CanBeCreated) override;

    std::map<std::string, MetarData>& GetMetarData() { return m_MetarData; }
    std::set<std::string>& GetDepAirports() { return m_DepAirports; }
    std::set<std::string>& GetArrAirports() { return m_ArrAirports; }
    std::set<std::string>& GetOnlineAirports() { return m_OnlineAirports; }
    std::map<std::string, DWORD>& GetNewTraffic() { return m_NewTraffic; }
    std::mutex& GetMutex() { return m_DataMutex; }
    int   GetPanelObjID() const { return PANEL_OBJ_ID; }
    FilterMode GetFilterMode() const { return m_FilterMode; }
    const std::set<std::string>& GetFilterIcaos() const { return m_FilterIcaos; }
    int& GetPanelX() { return m_PanelX; }
    int& GetPanelY() { return m_PanelY; }
    bool& GetCollapsed() { return m_Collapsed; }
    int& GetFontSize() { return m_FontSize; }
    void  SaveState();
    void  AcknowledgeAll();
    void  AcknowledgeOne(const std::string& icao);
    const char* L(const char* trText, const char* enText) const { return m_Language == Language::Turkish ? trText : enText; }
};
