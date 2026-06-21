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
    enum class FilterMode { All, Used, SingleIcao };
    enum class Source { Vatsim, Rasat };

private:
    struct MetarData { std::string ICAO, Wind, QNH, RawOutput; int alert = 0; }; // 0=yok 1=QNH 2=tum

    std::map<std::string, MetarData> m_MetarData;
    std::set<std::string>            m_DepAirports;
    std::set<std::string>            m_ArrAirports;
    std::map<std::string, DWORD>     m_NewTraffic;   // yeni gelen meydan -> GetTickCount (6sn mavi)
    std::mutex   m_DataMutex;
    bool         m_IsFetching;      // metar cekimi
    bool         m_FetchTraffic;    // trafik cekimi
    FilterMode   m_FilterMode;
    std::string  m_FilterIcao;        // (kullanilmiyor, geriye uyum)
    std::set<std::string> m_FilterIcaos;
    int          m_PanelX, m_PanelY;
    Source       m_Source;
    bool         m_Collapsed;
    int          m_FontSize;

    const int PANEL_OBJ_ID = 9001;

    std::string HttpGet(const wchar_t* host, const wchar_t* path, bool https = true);
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
};