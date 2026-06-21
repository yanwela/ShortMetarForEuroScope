#include "ShortMetar.h"
#include <winhttp.h>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace EuroScopePlugIn;
CShortMetar* g_pPlugin = nullptr;

// Rasat build id site guncellenince degisebilir; rasat calismayi birakirsa yeni id ile guncelle.
static const wchar_t* RASAT_BUILD = L"jsy7EZOlC7J2qCSq-y6KU";
static const char* LT_STATIONS =
"LTAC,LTAD,LTAE,LTAF,LTAG,LTAH,LTAI,LTAJ,LTAL,LTAN,LTAO,LTAP,LTAR,LTAS,LTAT,LTAU,LTAV,LTAW,LTAY,LTAZ,"
"LTBA,LTBB,LTBD,LTBE,LTBF,LTBG,LTBH,LTBI,LTBJ,LTBK,LTBL,LTBN,LTBO,LTBP,LTBQ,LTBR,LTBS,LTBT,LTBU,LTBV,LTBX,LTBY,LTBZ,"
"LTCA,LTCB,LTCC,LTCD,LTCE,LTCF,LTCG,LTCI,LTCJ,LTCK,LTCL,LTCM,LTCN,LTCO,LTCP,LTCR,LTCS,LTCT,LTCU,LTCV,LTCW,"
"LTDA,LTDB,LTFA,LTFB,LTFC,LTFD,LTFE,LTFG,LTFH,LTFJ,LTFK,LTFM,LTFO,LTHA,LTHB";

CShortMetar::CShortMetar()
    : CPlugIn(COMPATIBILITY_CODE, "ShortMetar", "1.0.0", "Alp", "Free to use"),
    m_IsFetching(false), m_FetchTraffic(false), m_FilterMode(FilterMode::All),
    m_PanelX(80), m_PanelY(80),
    m_Source(Source::Rasat), m_Collapsed(false), m_FontSize(13)
{
    g_pPlugin = this;
    LoadFont();
    LoadState();    // SMconfig.json'dan konum/font/kaynak yukle
    StartTrafficFetch();
    StartMetarFetch();
    DisplayUserMessage("ShortMetar", "Sistem",
        "ShortMetar yuklendi. Acilista tum LT* metarlari lislenedi. Komutlar icin: .sm help",
        true, true, true, false, false);
}

CShortMetar::~CShortMetar() { if (g_pPlugin == this) g_pPlugin = nullptr; }

CRadarScreen* CShortMetar::OnRadarScreenCreated(const char* sDisplayName,
    bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated)
{
    UNREFERENCED_PARAMETER(sDisplayName); UNREFERENCED_PARAMETER(NeedRadarContent);
    UNREFERENCED_PARAMETER(GeoReferenced); UNREFERENCED_PARAMETER(CanBeSaved);
    UNREFERENCED_PARAMETER(CanBeCreated);
    return new CMyRadarScreen();   // hangi radar ekrani olursa olsun overlay
}

void CShortMetar::StartMetarFetch()
{
    if (m_IsFetching) return;
    m_IsFetching = true;
    std::thread(&CShortMetar::FetchMetarAsync, this).detach();
}

void CShortMetar::StartTrafficFetch()
{
    if (m_FetchTraffic) return;
    m_FetchTraffic = true;
    std::thread(&CShortMetar::FetchTrafficAsync, this).detach();
}

// MGM ana sayfasindaki __NEXT_DATA__ icinden guncel buildId'yi cek (site guncellense de uyum saglar)
std::string CShortMetar::FetchRasatBuild()
{
    std::string html = HttpGet(L"rasat.mgm.gov.tr", L"/");
    const std::string key = "\"buildId\":\"";
    size_t p = html.find(key);
    if (p == std::string::npos) return "";
    p += key.size();
    size_t e = html.find('"', p);
    if (e == std::string::npos) return "";
    return html.substr(p, e - p);
}

// METAR'i secili kaynaktan (RASAT id'siz, ICAO ile / VATSIM bulk) ceker
void CShortMetar::FetchMetarAsync()
{
    try {
        if (m_Source == Source::Rasat) {
            std::string build = FetchRasatBuild();
            std::wstring wbuild = build.empty()
                ? std::wstring(RASAT_BUILD)
                : std::wstring(build.begin(), build.end());
            // ICAO listesini parcala (qs arrayLimit; guvenli icin 10'arlik gruplar, paralel istek)
            std::vector<std::string> all;
            {
                std::string s = LT_STATIONS; size_t st = 0;
                while (st < s.size()) {
                    size_t c = s.find(',', st);
                    all.push_back(s.substr(st, c == std::string::npos ? std::string::npos : c - st));
                    if (c == std::string::npos) break; st = c + 1;
                }
            }
            const size_t CHUNK = 10;
            std::vector<std::wstring> paths;
            for (size_t i = 0; i < all.size(); i += CHUNK) {
                std::wstring q;
                for (size_t j = i; j < all.size() && j < i + CHUNK; ++j)
                    if (!all[j].empty()) q += L"&stations=" + std::wstring(all[j].begin(), all[j].end());
                paths.push_back(L"/_next/data/" + wbuild + L"/result.json?obsType=1&hours=0" + q);
            }
            // tum gruplari AYNI ANDA (paralel) iste, hepsi gelince parse et
            std::vector<std::string> results(paths.size());
            std::vector<std::thread> ths;
            for (size_t k = 0; k < paths.size(); ++k)
                ths.emplace_back([this, &results, &paths, k]() { results[k] = HttpGet(L"rasat.mgm.gov.tr", paths[k].c_str()); });
            for (auto& t : ths) t.join();
            bool any = false;
            for (auto& r : results) if (!r.empty()) { ParseRasat(r); any = true; }
            if (any) DisplayUserMessage("ShortMetar", "RASAT", "RASAT guncellendi.", true, true, true, false, false);
            else     DisplayUserMessage("ShortMetar", "RASAT", "RASAT cekilemedi.", true, true, true, false, false);
        }
        else {
            std::string metar = HttpGet(L"metar.vatsim.net", L"/LT");
            if (!metar.empty()) { ParseBulkMetar(metar); DisplayUserMessage("ShortMetar", "VATSIM", "METAR guncellendi.", true, true, true, false, false); }
            else DisplayUserMessage("ShortMetar", "VATSIM", "Veri cekilemedi.", true, true, true, false, false);
        }
    }
    catch (...) {}
    m_IsFetching = false;   // her durumda bayragi birak (bir daha cekim bloklanmasin)
}

void CShortMetar::FetchTrafficAsync()
{
    try {
        std::string json = HttpGet(L"data.vatsim.net", L"/v3/vatsim-data.json");
        if (!json.empty()) ParseVatsimData(json);
    }
    catch (...) {}
    m_FetchTraffic = false;
}

std::string CShortMetar::PluginDir()
{
    HMODULE hSelf = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&g_pPlugin, &hSelf);
    char path[MAX_PATH] = { 0 };
    GetModuleFileNameA(hSelf, path, MAX_PATH);
    std::string p(path);
    size_t slash = p.find_last_of("\\/");
    return (slash == std::string::npos) ? "" : p.substr(0, slash + 1);
}

// Konum/font'u DLL klasorundeki SMconfig.json'a yaz
void CShortMetar::SaveState()
{
    std::string path = PluginDir() + "SMconfig.json";
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "w") == 0 && fp) {
        const char* src = (m_Source == Source::Vatsim) ? "vatsim" : "rasat";
        fprintf(fp, "{\n  \"x\": %d,\n  \"y\": %d,\n  \"fontSize\": %d,\n  \"source\": \"%s\"\n}\n",
            m_PanelX, m_PanelY, m_FontSize, src);
        fclose(fp);
    }
}

// SMconfig.json'dan konum/font/kaynak oku
void CShortMetar::LoadState()
{
    std::string path = PluginDir() + "SMconfig.json";
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "r") != 0 || !fp) return;
    std::string content; char b[256]; size_t n;
    while ((n = fread(b, 1, sizeof(b), fp)) > 0) content.append(b, n);
    fclose(fp);
    auto readInt = [&](const char* key, int def) -> int {
        size_t k = content.find(std::string("\"") + key + "\"");
        if (k == std::string::npos) return def;
        size_t c = content.find(':', k);
        if (c == std::string::npos) return def;
        return atoi(content.c_str() + c + 1);
        };
    auto readStr = [&](const char* key) -> std::string {
        size_t k = content.find(std::string("\"") + key + "\"");
        if (k == std::string::npos) return "";
        size_t c = content.find(':', k); if (c == std::string::npos) return "";
        size_t q1 = content.find('"', c + 1); if (q1 == std::string::npos) return "";
        size_t q2 = content.find('"', q1 + 1); if (q2 == std::string::npos) return "";
        return content.substr(q1 + 1, q2 - q1 - 1);
        };
    m_PanelX = readInt("x", m_PanelX);
    m_PanelY = readInt("y", m_PanelY);
    int f = readInt("fontSize", m_FontSize);
    m_FontSize = (f < 8 ? 8 : (f > 28 ? 28 : f));
    std::string src = readStr("source");
    if (src == "vatsim") m_Source = Source::Vatsim;
    else                 m_Source = Source::Rasat;   // varsayilan rasat
}

// EuroScope.ttf'yi DLL'in yanindan ozel font olarak yukle (DLL ile ayni klasore koy)
void CShortMetar::LoadFont()
{
    std::string fontPath = PluginDir() + "EuroScope.ttf";
    AddFontResourceExA(fontPath.c_str(), FR_PRIVATE, NULL);
}

void CShortMetar::AcknowledgeAll()
{
    std::lock_guard<std::mutex> lock(m_DataMutex);
    for (auto& kv : m_MetarData) kv.second.alert = 0;
}

void CShortMetar::OnTimer(int Counter)
{
    UNREFERENCED_PARAMETER(Counter);
    SYSTEMTIME st; GetSystemTime(&st);
    if (st.wSecond == 0) {
        int m = st.wMinute;
        bool inWindow = (m >= 20 && m <= 29) || (m >= 50 && m <= 59);
        bool fireMeta;
        if (inWindow) { int base = (m < 30) ? 20 : 50; fireMeta = ((m - base) % 3 == 0); }
        else { fireMeta = (m % 10 == 0); }
        if (fireMeta) StartMetarFetch();
    }
    // Trafik: her 2 dakikada bir (Counter saniyedir)
    if (Counter % 120 == 0) StartTrafficFetch();
}

void CShortMetar::ShowHelp()
{
    DisplayUserMessage("ShortMetar", "Yardim", ".sm all         -> TUM LT* meydan metarlari", true, true, true, false, false);
    DisplayUserMessage("ShortMetar", "Yardim", ".sm used        -> aktif trafik (kalkis+varis birlikte)", true, true, true, false, false);
    DisplayUserMessage("ShortMetar", "Yardim", ".sm filter ICAO -> tek meydan (orn .sm filter LTFM veya LTFM,LTAW,LTAI...)", true, true, true, false, false);
    DisplayUserMessage("ShortMetar", "Yardim", ".sm vatsim      -> kaynak VATSIM (varsayilan)", true, true, true, false, false);
    DisplayUserMessage("ShortMetar", "Yardim", ".sm rasat       -> kaynak RASAT (MGM)", true, true, true, false, false);
    DisplayUserMessage("ShortMetar", "Yardim", ".sm ack              -> tum sari uyarilari onayla (C butonu da ayni)", true, true, true, false, false);
    DisplayUserMessage("ShortMetar", "Yardim", ".sm refresh          -> METAR'i hemen guncelle", true, true, true, false, false);
    DisplayUserMessage("ShortMetar", "Yardim", ".sm refresh airport  -> trafik listesini hemen guncelle", true, true, true, false, false);
    DisplayUserMessage("ShortMetar", "Yardim", ".sm save             -> konum+font'u SMconfig.json'a kaydet", true, true, true, false, false);
    DisplayUserMessage("ShortMetar", "Yardim", ".sm reload           -> SMconfig.json'u yeniden yukle", true, true, true, false, false);
    DisplayUserMessage("ShortMetar", "Yardim", ".sm chatbox          -> panel gizliyse tekrar ac", true, true, true, false, false);
}

bool CShortMetar::OnCompileCommand(const char* sCommandLine)
{
    std::string raw(sCommandLine), lower = raw;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.rfind(".sm", 0) != 0) return false;

    std::string rest = lower.size() > 3 ? lower.substr(3) : "";
    size_t sp = rest.find_first_not_of(' ');
    rest = (sp == std::string::npos) ? "" : rest.substr(sp);

    if (rest.empty() || rest == "help") { ShowHelp(); return true; }
    if (rest == "show" || rest == "chatbox") { m_Collapsed = false; DisplayUserMessage("ShortMetar", "Panel", "Acildi.", true, true, true, false, false); return true; }
    if (rest == "save") { SaveState(); DisplayUserMessage("ShortMetar", "Config", "Konum ve font kaydedildi (SMconfig.json).", true, true, true, false, false); return true; }
    if (rest == "reload") { LoadState(); DisplayUserMessage("ShortMetar", "Config", "SMconfig.json yeniden yuklendi.", true, true, true, false, false); return true; }
    if (rest == "ack") { AcknowledgeAll(); DisplayUserMessage("ShortMetar", "ACK", "Tum uyarilar onaylandi.", true, true, true, false, false); return true; }
    if (rest == "refresh") { StartMetarFetch(); DisplayUserMessage("ShortMetar", "VATSIM", "METAR guncelleniyor...", true, true, true, false, false); return true; }
    if (rest == "refresh airport") { StartTrafficFetch(); DisplayUserMessage("ShortMetar", "VATSIM", "Trafik guncelleniyor...", true, true, true, false, false); return true; }
    if (rest == "all") { m_FilterMode = FilterMode::All;  DisplayUserMessage("ShortMetar", "Filtre", "Tum LT* meydanlar.", true, true, true, false, false); return true; }
    if (rest == "used") { m_FilterMode = FilterMode::Used; DisplayUserMessage("ShortMetar", "Filtre", "Aktif trafik (kalkis+varis).", true, true, true, false, false); return true; }
    if (rest == "vatsim") { m_Source = Source::Vatsim; SaveState(); StartMetarFetch(); DisplayUserMessage("ShortMetar", "Kaynak", "VATSIM.", true, true, true, false, false); return true; }
    if (rest == "rasat") { m_Source = Source::Rasat;  SaveState(); StartMetarFetch(); DisplayUserMessage("ShortMetar", "Kaynak", "RASAT (MGM).", true, true, true, false, false); return true; }
    if (rest.rfind("filter", 0) == 0) {
        std::string rr = raw.substr(3);
        size_t a = rr.find("filter"); std::string arg = (a == std::string::npos) ? "" : rr.substr(a + 6);
        // bosluk/tab at, buyuk harfe cevir
        std::string clean;
        for (char c : arg) if (c != ' ' && c != '\t') clean += (char)::toupper((unsigned char)c);
        if (clean.empty()) { DisplayUserMessage("ShortMetar", "Hata", "Kullanim: .sm filter LTFM  veya  .sm filter LTFM,LTAW,LTAI", true, true, true, false, false); return true; }
        m_FilterIcaos.clear();
        std::stringstream fs(clean); std::string item;
        while (std::getline(fs, item, ',')) if (!item.empty()) m_FilterIcaos.insert(item);
        m_FilterMode = FilterMode::SingleIcao;
        DisplayUserMessage("ShortMetar", "Filtre", (clean + " gosteriliyor.").c_str(), true, true, true, false, false);
        return true;
    }
    DisplayUserMessage("ShortMetar", "Hata", "Bilinmeyen komut. .sm help", true, true, true, false, false);
    return true;
}

std::string CShortMetar::HttpGet(const wchar_t* host, const wchar_t* path, bool https)
{
    std::string result;
    HINTERNET hSes = WinHttpOpen(L"ShortMetarPlugin/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSes) return result;
    WinHttpSetTimeouts(hSes, 8000, 8000, 8000, 8000);   // takilmayi onle: resolve/connect/send/receive 8sn
    HINTERNET hCon = WinHttpConnect(hSes, host, https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
    if (hCon) {
        HINTERNET hReq = WinHttpOpenRequest(hCon, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
        if (hReq) {
            if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(hReq, NULL)) {
                DWORD sz = 0;
                do {
                    if (WinHttpQueryDataAvailable(hReq, &sz) && sz > 0) {
                        std::vector<char> buf(sz + 1, 0); DWORD rd = 0;
                        if (WinHttpReadData(hReq, buf.data(), sz, &rd)) result.append(buf.data(), rd);
                    }
                } while (sz > 0);
            }
            WinHttpCloseHandle(hReq);
        }
        WinHttpCloseHandle(hCon);
    }
    WinHttpCloseHandle(hSes);
    return result;
}

// Rasat (MGM) Next.js veri endpoint'i. NOT: build id site guncellenince degisebilir;
// RASAT cevabindaki her "observationText":"METAR ... " icinden ICAO+ruzgar+QNH ayikla.
void CShortMetar::ParseRasat(const std::string& raw)
{
    std::lock_guard<std::mutex> lock(m_DataMutex);
    const std::string key = "\"observationText\":\"";
    size_t p = 0;
    while ((p = raw.find(key, p)) != std::string::npos) {
        p += key.size();
        size_t end = raw.find('"', p);
        if (end == std::string::npos) break;
        std::string text = raw.substr(p, end - p);   // METAR LTFG 201150Z 22008KT ... Q1014 ...
        p = end + 1;

        std::stringstream ls(text);
        std::string tok, icao, wind = "-", qnh = "-"; bool haveIcao = false;
        while (ls >> tok) {
            if (tok == "METAR" || tok == "SPECI") continue;
            if (!haveIcao) { if (tok.size() == 4 && tok.rfind("LT", 0) == 0) { icao = tok; haveIcao = true; } continue; }
            if (wind == "-" && tok.size() >= 5 && tok.substr(tok.size() - 2) == "KT") wind = tok;
            else if (tok.size() == 5 && tok[0] == 'Q') qnh = tok;
        }
        if (!haveIcao) continue;
        auto it = m_MetarData.find(icao);
        int alert = 0;
        if (it != m_MetarData.end()) {
            bool qnhCh = (it->second.QNH != "-" && qnh != "-" && it->second.QNH != qnh);
            bool windCh = (it->second.Wind != "-" && wind != "-" && it->second.Wind != wind);
            if (qnhCh && windCh) alert = 2;
            else if (qnhCh)      alert = 1;
            else                 alert = it->second.alert;
        }
        auto& d = m_MetarData[icao];
        d.ICAO = icao; d.Wind = wind; d.QNH = qnh;
        d.RawOutput = icao + " " + wind + " " + qnh;
        d.alert = alert;
    }
}

void CShortMetar::ParseVatsimData(const std::string& json)
{
    std::set<std::string> deps, arrs;
    size_t pos = 0;
    auto nextIcao = [&](size_t p) -> std::string {
        size_t q1 = json.find('"', p); if (q1 == std::string::npos) return "";
        size_t q2 = json.find('"', q1 + 1); if (q2 == std::string::npos) return "";
        return json.substr(q1 + 1, q2 - q1 - 1);
        };
    while ((pos = json.find("\"departure\":", pos)) != std::string::npos) {
        std::string ic = nextIcao(pos += 12);
        if (ic.size() == 4 && ic.rfind("LT", 0) == 0) deps.insert(ic);
    }
    pos = 0;
    while ((pos = json.find("\"arrival\":", pos)) != std::string::npos) {
        std::string ic = nextIcao(pos += 10);
        if (ic.size() == 4 && ic.rfind("LT", 0) == 0) arrs.insert(ic);
    }
    std::lock_guard<std::mutex> lock(m_DataMutex);
    DWORD now = GetTickCount();
    // yeni gelen meydanlari tespit et (onceden yoktu, simdi var)
    for (auto& ic : deps) if (!m_DepAirports.count(ic) && !m_ArrAirports.count(ic)) m_NewTraffic[ic] = now;
    for (auto& ic : arrs) if (!m_DepAirports.count(ic) && !m_ArrAirports.count(ic)) m_NewTraffic[ic] = now;
    m_DepAirports = std::move(deps);
    m_ArrAirports = std::move(arrs);
}

void CShortMetar::ParseBulkMetar(const std::string& raw)
{
    std::lock_guard<std::mutex> lock(m_DataMutex);
    std::stringstream ss(raw); std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        std::stringstream ls(line);
        std::string tok, icao, wind = "-", qnh = "-"; bool first = true;
        while (ls >> tok) {
            if (first) { icao = tok; first = false; continue; }
            if (tok.find("KT") != std::string::npos) wind = tok;
            else if (tok.size() == 5 && tok[0] == 'Q') qnh = tok;
        }
        if (icao.empty()) continue;
        auto it = m_MetarData.find(icao);
        int alert = 0;
        if (it != m_MetarData.end()) {
            bool qnhCh = (it->second.QNH != "-" && qnh != "-" && it->second.QNH != qnh);
            bool windCh = (it->second.Wind != "-" && wind != "-" && it->second.Wind != wind);
            if (qnhCh && windCh) alert = 2;
            else if (qnhCh)      alert = 1;
            else                 alert = it->second.alert;   // degisim yoksa mevcut uyari korunur
        }
        auto& d = m_MetarData[icao];
        d.ICAO = icao; d.Wind = wind; d.QNH = qnh;
        d.RawOutput = icao + " " + wind + " " + qnh;
        d.alert = alert;
    }
}

// ----------------------------- CMyRadarScreen -----------------------------
void CMyRadarScreen::OnAsrContentLoaded(bool Loaded)
{
    UNREFERENCED_PARAMETER(Loaded);
    RequestRefresh();   // CRadarScreen uyesi
}

void CMyRadarScreen::OnRefresh(HDC hDC, int Phase)
{
    if (Phase != REFRESH_PHASE_AFTER_LISTS) return;
    if (!g_pPlugin) return;

    const int RB = 14, TITLE_H = 16, PAD = 4;
    int px = g_pPlugin->GetPanelX(), py = g_pPlugin->GetPanelY();
    bool collapsed = g_pPlugin->GetCollapsed();
    int fh = g_pPlugin->GetFontSize();
    const int ROW = fh + 3;
    SetBkMode(hDC, TRANSPARENT);

    HFONT hTitleF = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_DONTCARE, "EuroScope");
    HFONT hContentF = CreateFontA(-fh, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_DONTCARE, "EuroScope");
    HFONT hOldF = (HFONT)SelectObject(hDC, hContentF);

    struct Row { std::string icao, wind, qnh; int alert; bool newTraffic; };
    std::vector<Row> rows; bool dataEmpty;
    {
        std::lock_guard<std::mutex> lock(g_pPlugin->GetMutex());
        auto& metar = g_pPlugin->GetMetarData();
        auto& deps = g_pPlugin->GetDepAirports();
        auto& arrs = g_pPlugin->GetArrAirports();
        auto& newTraf = g_pPlugin->GetNewTraffic();
        auto  mode = g_pPlugin->GetFilterMode();
        auto& fset = g_pPlugin->GetFilterIcaos();
        dataEmpty = metar.empty();
        DWORD now = GetTickCount();
        // 6 saniyesi dolan yeni trafik bildirimlerini temizle
        for (auto it = newTraf.begin(); it != newTraf.end(); ) {
            if (now - it->second > 6000) it = newTraf.erase(it); else ++it;
        }
        for (auto const& kv : metar) {
            const std::string& icao = kv.first; bool show;
            switch (mode) {
            case CShortMetar::FilterMode::Used:       show = deps.count(icao) > 0 || arrs.count(icao) > 0; break;
            case CShortMetar::FilterMode::SingleIcao: show = fset.count(icao) > 0; break;
            default:                                  show = true;
            }
            if (show) rows.push_back({ icao, kv.second.Wind, kv.second.QNH, kv.second.alert, newTraf.count(icao) > 0 });
        }
    }
    std::string note;
    if (rows.empty()) note = dataEmpty ? "Veri cekiliyor..." : "[Filtreye uyan meydan yok]";

    int contentW = 0;
    for (auto& r : rows) { std::string s = r.icao + " " + r.wind + " " + r.qnh; SIZE sz; GetTextExtentPoint32A(hDC, s.c_str(), (int)s.size(), &sz); if (sz.cx > contentW) contentW = sz.cx; }
    if (!note.empty()) { SIZE sz; GetTextExtentPoint32A(hDC, note.c_str(), (int)note.size(), &sz); if (sz.cx > contentW) contentW = sz.cx; }

    SelectObject(hDC, hTitleF);
    SIZE tt; GetTextExtentPoint32A(hDC, "ShortMetar", 10, &tt);
    int W = contentW + 2 * PAD;
    int minW = tt.cx + 4 * RB + 16; if (W < minW) W = minW;

    // === bar: [C] [ baslik ] [-] [+] [^] ===
    RECT titleR = { px, py, px + W, py + TITLE_H };
    HBRUSH hBg = CreateSolidBrush(RGB(58, 58, 58)); FillRect(hDC, &titleR, hBg); DeleteObject(hBg);
    HPEN hHi = CreatePen(PS_SOLID, 1, RGB(92, 92, 92)); HPEN hOldP = (HPEN)SelectObject(hDC, hHi);
    MoveToEx(hDC, px, py, NULL); LineTo(hDC, px + W, py);
    HPEN hLine = CreatePen(PS_SOLID, 1, RGB(18, 18, 18)); SelectObject(hDC, hLine);
    MoveToEx(hDC, px, py + TITLE_H - 1, NULL); LineTo(hDC, px + W, py + TITLE_H - 1);
    int xC = px + RB, xMinus = px + W - 3 * RB, xPlus = px + W - 2 * RB, xArrow = px + W - RB;
    int seps[4] = { xC, xMinus, xPlus, xArrow };
    for (int sx : seps) { MoveToEx(hDC, sx, py, NULL); LineTo(hDC, sx, py + TITLE_H); }
    SelectObject(hDC, hOldP); DeleteObject(hLine); DeleteObject(hHi);

    SetTextAlign(hDC, TA_LEFT | TA_TOP);
    int ty = py + (TITLE_H - tt.cy) / 2;
    auto glyph = [&](int cellX, const char* g) { SIZE s; GetTextExtentPoint32A(hDC, g, (int)strlen(g), &s); TextOutA(hDC, cellX + (RB - s.cx) / 2, ty, g, (int)strlen(g)); };
    SetTextColor(hDC, RGB(220, 220, 220)); glyph(px, "C");           // ACK butonu
    SetTextColor(hDC, RGB(255, 255, 255));
    int midL = px + RB, midR = px + W - 3 * RB;
    int tx = midL + ((midR - midL) - tt.cx) / 2; if (tx < midL + 2) tx = midL + 2;
    TextOutA(hDC, tx, ty, "ShortMetar", 10);
    SetTextColor(hDC, RGB(210, 210, 210));
    glyph(xMinus, "-"); glyph(xPlus, "+"); glyph(xArrow, collapsed ? "v" : "^");

    int id = g_pPlugin->GetPanelObjID();
    RECT ackR = { px, py, xC, py + TITLE_H };
    RECT dragR = { xC, py, xMinus, py + TITLE_H };
    RECT minR = { xMinus, py, xPlus, py + TITLE_H };
    RECT plusR = { xPlus, py, xArrow, py + TITLE_H };
    RECT arwR = { xArrow, py, px + W, py + TITLE_H };
    AddScreenObject(id, "ACK", ackR, false, "");
    AddScreenObject(id, "TITLEBAR", dragR, true, "");
    AddScreenObject(id, "MINUS", minR, false, "");
    AddScreenObject(id, "PLUS", plusR, false, "");
    AddScreenObject(id, "COLLAPSE", arwR, false, "");

    if (collapsed) { SelectObject(hDC, hOldF); DeleteObject(hTitleF); DeleteObject(hContentF); return; }

    // === icerik (tek sutun, asagi dogru) ===
    SelectObject(hDC, hContentF);
    const COLORREF WHITE = RGB(255, 255, 255), YELLOW = RGB(255, 220, 0), BLUE = RGB(80, 180, 255);
    int x = px + PAD, y = py + TITLE_H + 2;
    if (!note.empty()) {
        SetTextColor(hDC, RGB(150, 150, 150));
        TextOutA(hDC, x, y, note.c_str(), (int)note.size());
    }
    else {
        for (auto& r : rows) {
            if (r.newTraffic) {                       // yeni meydan -> tum satir mavi (6sn)
                SetTextColor(hDC, BLUE);
                std::string s = r.icao + " " + r.wind + " " + r.qnh;
                TextOutA(hDC, x, y, s.c_str(), (int)s.size());
            }
            else if (r.alert == 2) {                  // tum metar degisti -> tum satir sari
                SetTextColor(hDC, YELLOW);
                std::string s = r.icao + " " + r.wind + " " + r.qnh;
                TextOutA(hDC, x, y, s.c_str(), (int)s.size());
            }
            else if (r.alert == 1) {                  // sadece QNH degisti -> sadece QNH sari
                std::string pre = r.icao + " " + r.wind + " ";
                SetTextColor(hDC, WHITE);
                TextOutA(hDC, x, y, pre.c_str(), (int)pre.size());
                SIZE pw; GetTextExtentPoint32A(hDC, pre.c_str(), (int)pre.size(), &pw);
                SetTextColor(hDC, YELLOW);
                TextOutA(hDC, x + pw.cx, y, r.qnh.c_str(), (int)r.qnh.size());
            }
            else {
                SetTextColor(hDC, WHITE);
                std::string s = r.icao + " " + r.wind + " " + r.qnh;
                TextOutA(hDC, x, y, s.c_str(), (int)s.size());
            }
            y += ROW;
        }
    }
    SelectObject(hDC, hOldF); DeleteObject(hTitleF); DeleteObject(hContentF);
}

void CMyRadarScreen::OnClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
{
    UNREFERENCED_PARAMETER(Area); UNREFERENCED_PARAMETER(Button);
    if (!g_pPlugin || ObjectType != g_pPlugin->GetPanelObjID()) return;
    if (strcmp(sObjectId, "ACK") == 0) {
        g_pPlugin->AcknowledgeAll();   // C butonu -> tum uyarilari onayla
        RequestRefresh();
    }
    else if (strcmp(sObjectId, "COLLAPSE") == 0) {
        g_pPlugin->GetCollapsed() = !g_pPlugin->GetCollapsed();   // gizle/goster
        RequestRefresh();
    }
    else if (strcmp(sObjectId, "MINUS") == 0) {
        int& f = g_pPlugin->GetFontSize(); if (f > 8) f--; g_pPlugin->SaveState(); RequestRefresh();
    }
    else if (strcmp(sObjectId, "PLUS") == 0) {
        int& f = g_pPlugin->GetFontSize(); if (f < 28) f++; g_pPlugin->SaveState(); RequestRefresh();
    }
    else if (strcmp(sObjectId, "TITLEBAR") == 0) {
        m_GrabDX = Pt.x - g_pPlugin->GetPanelX();
        m_GrabDY = Pt.y - g_pPlugin->GetPanelY();
    }
}

void CMyRadarScreen::OnMoveScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, bool Released)
{
    UNREFERENCED_PARAMETER(Area);
    if (g_pPlugin && ObjectType == g_pPlugin->GetPanelObjID() && strcmp(sObjectId, "TITLEBAR") == 0) {
        g_pPlugin->GetPanelX() = Pt.x - m_GrabDX;
        g_pPlugin->GetPanelY() = Pt.y - m_GrabDY;
        RequestRefresh();
        if (Released) g_pPlugin->SaveState();   // birakinca konumu kaydet
    }
}

void __declspec(dllexport) EuroScopePlugInInit(CPlugIn** ppPlugInInstance) { *ppPlugInInstance = new CShortMetar(); }
void __declspec(dllexport) EuroScopePlugInExit(void) {}