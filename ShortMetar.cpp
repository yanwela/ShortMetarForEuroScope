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
static const wchar_t* RASAT_BUILD = L"T8mkW-IVCJLnn0LMMsebO";
static const char* RASAT_BUILD_STR = "T8mkW-IVCJLnn0LMMsebO";
static const char* LT_STATIONS =
"LTAC,LTAD,LTAE,LTAF,LTAG,LTAH,LTAI,LTAJ,LTAL,LTAN,LTAO,LTAP,LTAR,LTAS,LTAT,LTAU,LTAV,LTAW,LTAY,LTAZ,"
"LTBA,LTBB,LTBD,LTBE,LTBF,LTBG,LTBH,LTBI,LTBJ,LTBK,LTBL,LTBN,LTBO,LTBP,LTBQ,LTBR,LTBS,LTBT,LTBU,LTBV,LTBX,LTBY,LTBZ,"
"LTCA,LTCB,LTCC,LTCD,LTCE,LTCF,LTCG,LTCI,LTCJ,LTCK,LTCL,LTCM,LTCN,LTCO,LTCP,LTCR,LTCS,LTCT,LTCU,LTCV,LTCW,"
"LTDA,LTDB,LTFA,LTFB,LTFC,LTFD,LTFE,LTFG,LTFH,LTFJ,LTFK,LTFM,LTFO,LTHA,LTHB";

CShortMetar::CShortMetar()
    : CPlugIn(COMPATIBILITY_CODE, "ShortMetar", "1.1.0", "Alp", "Free to use"),
    m_IsFetching(false), m_FetchTraffic(false), m_FilterMode(FilterMode::All),
    m_PanelX(80), m_PanelY(80),
    m_Source(Source::Rasat), m_Collapsed(false), m_FontSize(13)
{
    g_pPlugin = this;
    LoadFont();
    LoadState();    // SMconfig.json'dan konum/font/kaynak yukle
    LoadSectorData();   // .ese dosyasindan pozisyon/sektor kapsama verisini yukle
    StartTrafficFetch();
    StartMetarFetch();
    DisplayUserMessage("ShortMetar", L("Sistem", "System"),
        L("ShortMetar yuklendi. Acilista tum LT* metarlari lislenedi. Komutlar icin: .sm help",
          "ShortMetar loaded. All LT* metars listed at startup. For commands: .sm help"),
        true, true, true, false, false);
    DisplayUserMessage("ShortMetar", L("Sistem", "System"),
        L(".sm online kullanmak icin once VATSIM'e (EuroScope) baglanin.",
          "For use .sm online, connect VATSIM (EuroScope) first."),
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
    m_MetarFetchStartTick = GetTickCount();
    std::thread(&CShortMetar::FetchMetarAsync, this).detach();
}

void CShortMetar::StartTrafficFetch()
{
    if (m_FetchTraffic) return;
    m_FetchTraffic = true;
    m_TrafficFetchStartTick = GetTickCount();
    std::thread(&CShortMetar::FetchTrafficAsync, this).detach();
}

// Build id'yi calisan /result sayfasindan bul ("/" ana sayfa guvenilir degildi, /result SSR calisiyor)
std::string CShortMetar::FetchRasatBuild()
{
    std::string html = HttpGet(L"rasat.mgm.gov.tr", L"/result?hours=0&obsType=1&stations=LTFM");
    const std::string key = "\"buildId\":\"";
    size_t p = html.find(key);
    if (p != std::string::npos) {
        p += key.size();
        size_t e = html.find('"', p);
        if (e != std::string::npos) {
            std::string id = html.substr(p, e - p);
            if (id.size() > 4) return id;
        }
    }
    return "";   // caller hardcoded RASAT_BUILD'i kullanacak
}

// /result HTML'inden (veya herhangi bir metinden) ilk "METAR/SPECI LTxx ..." satirini bulup ICAO+ruzgar+QNH ayikla
static bool ParseMetarLine(const std::string& html, std::string& icao, std::string& wind, std::string& qnh)
{
    size_t p = html.find("METAR LT");
    size_t sp = html.find("SPECI LT");
    if (sp != std::string::npos && (p == std::string::npos || sp < p)) p = sp;
    if (p == std::string::npos) return false;
    size_t e = p;
    while (e < html.size() && e < p + 300 && html[e] != '<' && html[e] != '\n' && html[e] != '\r') e++;
    std::string text = html.substr(p, e - p);

    std::stringstream ls(text);
    std::string tok; icao.clear(); wind = "-"; qnh = "-"; bool haveIcao = false;
    while (ls >> tok) {
        if (tok == "METAR" || tok == "SPECI") continue;
        if (!haveIcao) { if (tok.size() == 4 && tok.rfind("LT", 0) == 0) { icao = tok; haveIcao = true; } continue; }
        if (wind == "-" && tok.size() >= 5 && tok.size() <= 11 && tok.substr(tok.size() - 2) == "KT") wind = tok;
        else if (qnh == "-" && tok.size() == 5 && tok[0] == 'Q') qnh = tok;
    }
    return haveIcao;
}

// METAR'i secili kaynaktan ceker (RASAT: /result HTML sayfasi, build id gerekmez / VATSIM: bulk)
void CShortMetar::FetchMetarAsync()
{
    try {
        if (m_Source == Source::Rasat) {
            // build id'yi onbellekten kullan; yoksa bir kez kesfet ve onbellege al (her fetch'te tekrar cekmesin)
            if (m_CachedRasatBuild.empty()) {
                m_CachedRasatBuild = FetchRasatBuild();
                std::string dbg = PluginDir() + "SM_rasat_buildid.txt";
                FILE* fp = nullptr;
                if (fopen_s(&fp, dbg.c_str(), "w") == 0 && fp) {
                    fprintf(fp, "discovered=%s\nfallback=%s\nusing=%s\n",
                        m_CachedRasatBuild.empty() ? "(bulunamadi)" : m_CachedRasatBuild.c_str(),
                        RASAT_BUILD_STR,
                        m_CachedRasatBuild.empty() ? RASAT_BUILD_STR : m_CachedRasatBuild.c_str());
                    fclose(fp);
                }
            }
            std::wstring wbuild = m_CachedRasatBuild.empty()
                ? std::wstring(RASAT_BUILD)
                : std::wstring(m_CachedRasatBuild.begin(), m_CachedRasatBuild.end());
            std::vector<std::string> all;
            {
                std::string s = LT_STATIONS; size_t st = 0;
                while (st < s.size()) {
                    size_t c = s.find(',', st);
                    all.push_back(s.substr(st, c == std::string::npos ? std::string::npos : c - st));
                    if (c == std::string::npos) break; st = c + 1;
                }
            }
            // _next/data/<buildId>/result.json istek basina EN FAZLA 10 istasyonu doner;
            // fazlasi istenirse 11.'den itibaren SESSIZCE (hatasiz) dusuyor. 20 ile test edildiginde
            // her grubun yarisi hic gelmiyordu -> CHUNK 10'a sabitlendi.
            const size_t CHUNK = 10;
            std::vector<std::wstring> paths;
            for (size_t i = 0; i < all.size(); i += CHUNK) {
                std::wstring q;
                for (size_t j = i; j < all.size() && j < i + CHUNK; ++j)
                    if (!all[j].empty()) q += L"&stations=" + std::wstring(all[j].begin(), all[j].end());
                paths.push_back(L"/_next/data/" + wbuild + L"/result.json?obsType=1&hours=0" + q);
            }
            // NO_PROXY sonrasi tek baglanti yeterince hizli; paralel worker'lar bazi gruplarin
            // sessizce bos donmesine (MGM tarafi es zamanli baglanti kisitlamasi olabilir) yol aciyordu.
            // Tum gruplari TEK baglanti uzerinden sirayla cek -> hicbiri atlanmaz.
            std::vector<std::string> results(paths.size());
            HttpGetMulti(L"rasat.mgm.gov.tr", paths, results, 0);
            bool any = false;
            int emptyCount = 0;
            for (auto& r : results) { if (!r.empty()) { ParseRasat(r); any = true; } else emptyCount++; }
            if (any) {
                char msg[96]; sprintf_s(msg, L("RASAT guncellendi (%d/%d grup bos).", "RASAT updated (%d/%d groups empty)."), emptyCount, (int)results.size());
                DisplayUserMessage("ShortMetar", "RASAT", emptyCount > 0 ? msg : L("RASAT guncellendi.", "RASAT updated."), true, true, true, false, false);
            }
            else {
                DisplayUserMessage("ShortMetar", "RASAT", L("RASAT cekilemedi.", "RASAT fetch failed."), true, true, true, false, false);
                m_CachedRasatBuild.clear();   // basarisizsa onbellegi temizle, bir sonrakinde yeniden kesfetsin
            }
        }
        else {
            std::string metar = HttpGet(L"metar.vatsim.net", L"/LT");
            if (!metar.empty()) { ParseBulkMetar(metar); DisplayUserMessage("ShortMetar", "VATSIM", L("METAR guncellendi.", "METAR updated."), true, true, true, false, false); }
            else DisplayUserMessage("ShortMetar", "VATSIM", L("Veri cekilemedi.", "Data fetch failed."), true, true, true, false, false);
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
        const char* lang = (m_Language == Language::Turkish) ? "tr" : "eng";
        fprintf(fp, "{\n  \"x\": %d,\n  \"y\": %d,\n  \"fontSize\": %d,\n  \"source\": \"%s\",\n  \"language\": \"%s\"\n}\n",
            m_PanelX, m_PanelY, m_FontSize, src, lang);
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
    std::string lang = readStr("language");
    if (lang == "tr") m_Language = Language::Turkish;
    else              m_Language = Language::English;   // varsayilan ingilizce
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

// Basit satir okuyucu: \r ve bastaki/sondaki bosluklari temizler
static std::string TrimLine(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (b > a && (s[b - 1] == '\r' || s[b - 1] == '\n' || s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    return s.substr(a, b - a);
}

// Verilen klasorde ilk *.ese dosyasini bulur (AIRAC surumune gore dosya adi degisir, o yuzden sabit ad aranmaz)
static std::string FindFirstEseFileIn(const std::string& dir)
{
    std::string pattern = dir + "*.ese";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return "";
    std::string name = fd.cFileName;
    FindClose(h);
    return dir + name;
}

// AIRAC paketi genelde <kok>\LTXX\Plugins\ShortMetar\ (=PluginDir()) altinda calisir; .ese ise LTXX klasorunun
// ICINDE degil, <kok>\ altinda (LTXX ile ayni seviyede) durur -> PluginDir()'den 3 ust dizin. Kok klasor adi
// kullanicidan kullaniciya degistigi icin sirayla PluginDir(), 1, 2 ve 3 ust dizin denenir.
static std::string FindFirstEseFile(const std::string& pluginDir)
{
    std::string found = FindFirstEseFileIn(pluginDir);
    if (!found.empty()) return found;
    found = FindFirstEseFileIn(pluginDir + "..\\");
    if (!found.empty()) return found;
    found = FindFirstEseFileIn(pluginDir + "..\\..\\");
    if (!found.empty()) return found;
    return FindFirstEseFileIn(pluginDir + "..\\..\\..\\");
}

// .ese dosyasindan [POSITIONS] (callsign->kisa kimlik) ve [AIRSPACE] (SECTOR/OWNER/DEPAPT/ARRAPT) verisini yukler.
// Boylece "hangi pozisyon (orn. W78) hangi meydanlardan sorumlu" gercek sektor verisinden (poligon hesabi olmadan) cikarilir.
// Tanı: PluginDir(), bulunan .ese yolu ve parse sonrasi sayilari SM_sector_debug.txt'ye yazar (sorun tespiti icin)
static void WriteSectorDebug(const std::string& pluginDir, const std::string& esePath, size_t posCount, size_t covCount)
{
    std::string dbg = pluginDir + "SM_sector_debug.txt";
    FILE* fp = nullptr;
    if (fopen_s(&fp, dbg.c_str(), "w") == 0 && fp) {
        fprintf(fp, "pluginDir=%s\nesePath=%s\npositions=%zu\nsectorIdentifiers=%zu\n",
            pluginDir.c_str(), esePath.empty() ? "(bulunamadi)" : esePath.c_str(), posCount, covCount);
        fclose(fp);
    }
}

void CShortMetar::LoadSectorData()
{
    if (m_SectorDataLoaded) return;
    std::string dir = PluginDir();
    std::string path = FindFirstEseFile(dir);
    if (path.empty()) { WriteSectorDebug(dir, path, 0, 0); return; }
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "rb") != 0 || !fp) { WriteSectorDebug(dir, path, 0, 0); return; }
    std::string content; char b[8192]; size_t n;
    while ((n = fread(b, 1, sizeof(b), fp)) > 0) content.append(b, n);
    fclose(fp);

    std::stringstream ss(content);
    std::string line;
    std::string section;
    std::vector<std::string> curOwners;
    std::set<std::string> curApts;
    bool inBlock = false;

    auto flushBlock = [&]() {
        for (auto& id : curOwners) m_SectorCoverage[id].insert(curApts.begin(), curApts.end());
        curOwners.clear(); curApts.clear(); inBlock = false;
        };

    while (std::getline(ss, line)) {
        line = TrimLine(line);
        if (line.empty()) { if (inBlock) flushBlock(); continue; }
        if (line[0] == '[') { if (inBlock) flushBlock(); section = line; continue; }
        if (line[0] == ';') continue;

        if (section == "[POSITIONS]") {
            // CALLSIGN:REALNAME:FREQ:IDENT:SUFFIX:PREFIX:FACILITY:...
            size_t c1 = line.find(':'); if (c1 == std::string::npos) continue;
            size_t c2 = line.find(':', c1 + 1); if (c2 == std::string::npos) continue;
            size_t c3 = line.find(':', c2 + 1); if (c3 == std::string::npos) continue;
            size_t c4 = line.find(':', c3 + 1);
            size_t c5 = (c4 == std::string::npos) ? std::string::npos : line.find(':', c4 + 1);
            size_t c6 = (c5 == std::string::npos) ? std::string::npos : line.find(':', c5 + 1);
            std::string callsign = line.substr(0, c1);
            std::string ident = line.substr(c3 + 1, (c4 == std::string::npos ? line.size() : c4) - c3 - 1);
            std::transform(callsign.begin(), callsign.end(), callsign.begin(), ::toupper);
            if (!callsign.empty() && !ident.empty()) m_PositionIdentifier[callsign] = ident;
            if (!ident.empty() && c5 != std::string::npos) {
                std::string prefix = line.substr(c5 + 1, (c6 == std::string::npos ? line.size() : c6) - c5 - 1);
                if (!prefix.empty()) m_PositionAirportHint[ident] = prefix;
            }
            continue;
        }

        if (section == "[AIRSPACE]") {
            if (line.rfind("SECTOR:", 0) == 0) {
                if (inBlock) flushBlock();
                inBlock = true;
                continue;
            }
            if (!inBlock) continue;
            if (line.rfind("OWNER:", 0) == 0) {
                std::stringstream os(line.substr(6)); std::string id;
                while (std::getline(os, id, ':')) if (!id.empty()) curOwners.push_back(id);
                continue;
            }
            if (line.rfind("DEPAPT:", 0) == 0 || line.rfind("ARRAPT:", 0) == 0) {
                std::string apt = line.substr(line.find(':') + 1);
                if (apt.size() == 4) curApts.insert(apt);
                continue;
            }
            // BORDER/DISPLAY/diger satirlar: kapsama hesabi icin gerek yok, yoksay
        }
    }
    if (inBlock) flushBlock();
    m_SectorDataLoaded = true;
    WriteSectorDebug(dir, path, m_PositionIdentifier.size(), m_SectorCoverage.size());
}

// Bir kimligin kapsadigi meydanlari dondurur: once OWNER zincirinden gelen gercek sektor kapsami,
// bosa (orn. FMP/ATIS gibi hava sahasi sahiplenmeyen pozisyonlar) ise PREFIX alanindaki tek meydana duser.
std::set<std::string> CShortMetar::ResolveSectorAirports(const std::string& identifier) const
{
    auto it = m_SectorCoverage.find(identifier);
    if (it != m_SectorCoverage.end() && !it->second.empty()) return it->second;
    auto itHint = m_PositionAirportHint.find(identifier);
    if (itHint != m_PositionAirportHint.end() && itHint->second.size() == 4 && itHint->second.rfind("LT", 0) == 0)
        return { itHint->second };
    return {};
}

// Aktif online/sct filtresini (own=kendi pozisyonum ya da sabit bir kimlik) gecerli kapsama verisinden yeniden hesaplar.
void CShortMetar::RecomputeOnlineFilter()
{
    std::string identifier = m_OnlineIdentifier;
    if (m_OnlineIsOwn) {
        EuroScopePlugIn::CController me = ControllerMyself();
        if (!me.IsValid()) return;
        std::string cs = me.GetCallsign();
        std::transform(cs.begin(), cs.end(), cs.begin(), ::toupper);
        auto it = m_PositionIdentifier.find(cs);
        if (it == m_PositionIdentifier.end()) return;
        identifier = it->second;
    }
    std::set<std::string> coverage = ResolveSectorAirports(identifier);
    std::lock_guard<std::mutex> lock(m_DataMutex);
    m_OnlineAirports = std::move(coverage);
}

void CShortMetar::OnTimer(int Counter)
{
    UNREFERENCED_PARAMETER(Counter);
    // Watchdog: bir cekim 15 saniyeden uzun surerse (donmus/crash olmus olabilir) bayragi zorla birak
    DWORD now = GetTickCount();
    if (m_IsFetching && m_MetarFetchStartTick != 0 && (now - m_MetarFetchStartTick) > 35000) {
        m_IsFetching = false;
        DisplayUserMessage("ShortMetar", L("Uyari", "Warning"), L("METAR cekimi takildi, sifirlandi. Tekrar deneniyor...", "METAR fetch got stuck, reset. Retrying..."), true, true, true, false, false);
        StartMetarFetch();
    }
    if (m_FetchTraffic && m_TrafficFetchStartTick != 0 && (now - m_TrafficFetchStartTick) > 15000) {
        m_FetchTraffic = false;
    }

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
    // Online/sct filtresini tazele: 30 saniyede bir (sadece Online modunda; "own" ise CID/pozisyon degisimini yakalar)
    if (m_FilterMode == FilterMode::Online && Counter % 30 == 0) RecomputeOnlineFilter();
}

void CShortMetar::ShowHelp()
{
    const char* tag = L("Yardim", "Help");
    DisplayUserMessage("ShortMetar", tag, L(".sm all         -> TUM LT* meydan metarlari", ".sm all         -> ALL LT* airport metars"), true, true, true, false, false);
    DisplayUserMessage("ShortMetar", tag, L(".sm used        -> aktif trafik (kalkis+varis birlikte)", ".sm used        -> active traffic (departures+arrivals combined)"), true, true, true, false, false);
    DisplayUserMessage("ShortMetar", tag, L(".sm online      -> kendi CID'inizin bagli oldugu sektordeki aktif trafik", ".sm online      -> active traffic in the sector your CID is currently connected to"), true, true, true, false, false);
    DisplayUserMessage("ShortMetar", tag, L(".sm online sct ANK_W78_CTR (veya kisaca W78) -> belirli bir sektordeki aktif trafik", ".sm online sct ANK_W78_CTR (or short form W78) -> active traffic in a specific sector"), true, true, true, false, false);
    DisplayUserMessage("ShortMetar", tag, L(".sm filter ICAO -> tek meydan (orn .sm filter LTFM veya LTFM,LTAW,LTAI...)", ".sm filter ICAO -> single airport (e.g. .sm filter LTFM or LTFM,LTAW,LTAI...)"), true, true, true, false, false);
    DisplayUserMessage("ShortMetar", tag, L(".sm vatsim      -> kaynak VATSIM (varsayilan)", ".sm vatsim      -> source VATSIM (default)"), true, true, true, false, false);
    DisplayUserMessage("ShortMetar", tag, L(".sm rasat       -> kaynak RASAT (MGM)", ".sm rasat       -> source RASAT (MGM)"), true, true, true, false, false);
    DisplayUserMessage("ShortMetar", tag, L(".sm eng              -> dili Ingilizce yap (varsayilan)", ".sm eng              -> switch language to English (default)"), true, true, true, false, false);
    DisplayUserMessage("ShortMetar", tag, L(".sm tr               -> dili Turkce yap", ".sm tr               -> switch language to Turkish"), true, true, true, false, false);
    DisplayUserMessage("ShortMetar", tag, L(".sm ack              -> tum sari uyarilari onayla (C butonu da ayni)", ".sm ack              -> acknowledge all yellow alerts (C button does the same)"), true, true, true, false, false);
    DisplayUserMessage("ShortMetar", tag, L(".sm refresh          -> METAR'i hemen guncelle", ".sm refresh          -> update METAR now"), true, true, true, false, false);
    DisplayUserMessage("ShortMetar", tag, L(".sm refresh airport  -> trafik listesini hemen guncelle", ".sm refresh airport  -> update traffic list now"), true, true, true, false, false);
    DisplayUserMessage("ShortMetar", tag, L(".sm save             -> konum+font'u SMconfig.json'a kaydet", ".sm save             -> save position+font to SMconfig.json"), true, true, true, false, false);
    DisplayUserMessage("ShortMetar", tag, L(".sm reload           -> SMconfig.json'u yeniden yukle", ".sm reload           -> reload SMconfig.json"), true, true, true, false, false);
    DisplayUserMessage("ShortMetar", tag, L(".sm chatbox          -> panel gizliyse tekrar ac", ".sm chatbox          -> reopen panel if hidden"), true, true, true, false, false);
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
    if (rest == "show" || rest == "chatbox") { m_Collapsed = false; DisplayUserMessage("ShortMetar", L("Panel", "Panel"), L("Acildi.", "Opened."), true, true, true, false, false); return true; }
    if (rest == "save") { SaveState(); DisplayUserMessage("ShortMetar", L("Config", "Config"), L("Konum ve font kaydedildi (SMconfig.json).", "Position and font saved (SMconfig.json)."), true, true, true, false, false); return true; }
    if (rest == "reload") { LoadState(); DisplayUserMessage("ShortMetar", L("Config", "Config"), L("SMconfig.json yeniden yuklendi.", "SMconfig.json reloaded."), true, true, true, false, false); return true; }
    if (rest == "ack") { AcknowledgeAll(); DisplayUserMessage("ShortMetar", "ACK", L("Tum uyarilar onaylandi.", "All alerts acknowledged."), true, true, true, false, false); return true; }
    if (rest == "eng") { m_Language = Language::English; SaveState(); DisplayUserMessage("ShortMetar", "Language", "English.", true, true, true, false, false); return true; }
    if (rest == "tr") { m_Language = Language::Turkish; SaveState(); DisplayUserMessage("ShortMetar", "Dil", "Turkce.", true, true, true, false, false); return true; }
    if (rest == "refresh") {
        StartMetarFetch();
        const char* srcTag = (m_Source == Source::Rasat) ? "RASAT" : "VATSIM";
        DisplayUserMessage("ShortMetar", srcTag, L("METAR guncelleniyor...", "Updating METAR..."), true, true, true, false, false);
        return true;
    }
    if (rest == "refresh airport") { StartTrafficFetch(); DisplayUserMessage("ShortMetar", "VATSIM", L("Trafik guncelleniyor...", "Updating traffic..."), true, true, true, false, false); return true; }
    if (rest == "all") { m_FilterMode = FilterMode::All;  DisplayUserMessage("ShortMetar", L("Filtre", "Filter"), L("Tum LT* meydanlar.", "All LT* airports."), true, true, true, false, false); return true; }
    if (rest == "used") { m_FilterMode = FilterMode::Used; DisplayUserMessage("ShortMetar", L("Filtre", "Filter"), L("Aktif trafik (kalkis+varis).", "Active traffic (departures+arrivals)."), true, true, true, false, false); return true; }
    if (rest == "online" || rest.rfind("online ", 0) == 0) {
        std::vector<std::string> toks; { std::stringstream ts(rest); std::string t; while (ts >> t) toks.push_back(t); }
        bool named = toks.size() >= 3 && toks[1] == "sct";
        std::string arg;
        if (named) {
            arg = toks[2];
            std::transform(arg.begin(), arg.end(), arg.begin(), ::toupper);
        }
        std::string identifier;
        if (named) {
            auto itPos = m_PositionIdentifier.find(arg);
            identifier = (itPos != m_PositionIdentifier.end()) ? itPos->second : arg;
        }
        else {
            EuroScopePlugIn::CController me = ControllerMyself();
            if (!me.IsValid()) {
                DisplayUserMessage("ShortMetar", L("Hata", "Error"), L("Bagli degilsiniz.", "You are not connected."), true, true, true, false, false);
                return true;
            }
            std::string cs = me.GetCallsign();
            std::transform(cs.begin(), cs.end(), cs.begin(), ::toupper);
            auto itPos = m_PositionIdentifier.find(cs);
            if (itPos == m_PositionIdentifier.end()) {
                DisplayUserMessage("ShortMetar", L("Hata", "Error"), L("Pozisyonunuz sektor verisinde bulunamadi.", "Your position wasn't found in sector data."), true, true, true, false, false);
                return true;
            }
            identifier = itPos->second;
        }
        std::set<std::string> coverage = ResolveSectorAirports(identifier);
        if (coverage.empty()) {
            char msg[128]; sprintf_s(msg, L("Sektor bulunamadi: %s", "Sector not found: %s"), identifier.c_str());
            DisplayUserMessage("ShortMetar", L("Hata", "Error"), msg, true, true, true, false, false);
            return true;
        }
        m_OnlineIsOwn = !named;
        m_OnlineIdentifier = identifier;
        m_FilterMode = FilterMode::Online;
        RecomputeOnlineFilter();
        char msg[128]; sprintf_s(msg, L("Sektor %s: %d meydan.", "Sector %s: %d airports."), identifier.c_str(), (int)m_OnlineAirports.size());
        DisplayUserMessage("ShortMetar", L("Filtre", "Filter"), msg, true, true, true, false, false);
        return true;
    }
    if (rest == "vatsim") { m_Source = Source::Vatsim; SaveState(); StartMetarFetch(); DisplayUserMessage("ShortMetar", L("Kaynak", "Source"), "VATSIM.", true, true, true, false, false); return true; }
    if (rest == "rasat") { m_Source = Source::Rasat;  SaveState(); StartMetarFetch(); DisplayUserMessage("ShortMetar", L("Kaynak", "Source"), "RASAT (MGM).", true, true, true, false, false); return true; }
    if (rest.rfind("filter", 0) == 0) {
        std::string rr = raw.substr(3);
        size_t a = rr.find("filter"); std::string arg = (a == std::string::npos) ? "" : rr.substr(a + 6);
        // bosluk/tab at, buyuk harfe cevir
        std::string clean;
        for (char c : arg) if (c != ' ' && c != '\t') clean += (char)::toupper((unsigned char)c);
        if (clean.empty()) { DisplayUserMessage("ShortMetar", L("Hata", "Error"), L("Kullanim: .sm filter LTFM  veya  .sm filter LTFM,LTAW,LTAI", "Usage: .sm filter LTFM  or  .sm filter LTFM,LTAW,LTAI"), true, true, true, false, false); return true; }
        m_FilterIcaos.clear();
        std::stringstream fs(clean); std::string item;
        while (std::getline(fs, item, ',')) if (!item.empty()) m_FilterIcaos.insert(item);
        m_FilterMode = FilterMode::SingleIcao;
        DisplayUserMessage("ShortMetar", L("Filtre", "Filter"), (clean + L(" gosteriliyor.", " shown.")).c_str(), true, true, true, false, false);
        return true;
    }
    DisplayUserMessage("ShortMetar", L("Hata", "Error"), L("Bilinmeyen komut. .sm help", "Unknown command. .sm help"), true, true, true, false, false);
    return true;
}

std::string CShortMetar::HttpGet(const wchar_t* host, const wchar_t* path, bool https)
{
    std::string result;
    HINTERNET hSes = WinHttpOpen(L"ShortMetarPlugin/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
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

// Ayni sunucuya birden fazla istegi TEK baglanti (session+connect, keep-alive) uzerinden sirayla cek.
// Her istekte yeniden TLS el sikismasi yapmaktan cok daha hizli.
void CShortMetar::HttpGetMulti(const wchar_t* host, const std::vector<std::wstring>& paths, std::vector<std::string>& outResults, size_t startIdx, bool https)
{
    HINTERNET hSes = WinHttpOpen(L"ShortMetarPlugin/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSes) return;
    WinHttpSetTimeouts(hSes, 8000, 8000, 8000, 8000);
    HINTERNET hCon = WinHttpConnect(hSes, host, https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
    if (hCon) {
        for (size_t i = 0; i < paths.size(); ++i) {
            std::string result;
            HINTERNET hReq = WinHttpOpenRequest(hCon, L"GET", paths[i].c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
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
            outResults[startIdx + i] = std::move(result);
        }
        WinHttpCloseHandle(hCon);
    }
    WinHttpCloseHandle(hSes);
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
            if (wind == "-" && tok.size() >= 5 && tok.size() <= 11 && tok.substr(tok.size() - 2) == "KT") wind = tok;
            else if (qnh == "-" && tok.size() == 5 && tok[0] == 'Q') qnh = tok;
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
            if (wind == "-" && tok.size() >= 5 && tok.size() <= 11 && tok.substr(tok.size() - 2) == "KT") wind = tok;
            else if (qnh == "-" && tok.size() == 5 && tok[0] == 'Q') qnh = tok;
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
        auto& online = g_pPlugin->GetOnlineAirports();
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
            case CShortMetar::FilterMode::Online:     show = online.count(icao) > 0 && (deps.count(icao) > 0 || arrs.count(icao) > 0); break;
            case CShortMetar::FilterMode::SingleIcao: show = fset.count(icao) > 0; break;
            default:                                  show = true;
            }
            if (show) rows.push_back({ icao, kv.second.Wind, kv.second.QNH, kv.second.alert, newTraf.count(icao) > 0 });
        }
    }
    std::string note;
    if (rows.empty()) note = dataEmpty ? g_pPlugin->L("Veri cekiliyor...", "Fetching data...") : g_pPlugin->L("[Filtreye uyan meydan yok]", "[No airport matches filter]");

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
