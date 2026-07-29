# ShortMetarForEuroScope

ShortMetar, EuroScope içerisinde Türk meydanlarının METAR bilgilerini hızlı ve kompakt şekilde görüntülemek için geliştirilmiş bir eklentidir. Veriler VATSIM veya MGM RASAT kaynaklarından alınabilir ve aktif trafik bilgileriyle birlikte filtrelenebilir. Panel sürüklenebilir, küçültülebilir, yazı boyutu değiştirilebilir ve ayarlar otomatik olarak saklanabilir.

## Komutlar

`.sm all` — **Önerilen komut.** Tüm LT* meydanlarının METAR bilgilerini gösterir.

`.sm used` — **Önerilen komut.** Sadece aktif kalkış veya varış trafiği bulunan meydanları gösterir.

`.sm filter LTFM` — İstenilen tek bir meydanı gösterir.

`.sm filter LTFM,LTAW,LTAI` — İstenilen birden fazla meydanı gösterir.

`.sm online` — **Önerilen komut.** Bağlı olduğunuz CID'in sorumlu olduğu sektördeki aktif trafiği gösterir.

`.sm online sct ANK_W78` (kısaca `.sm online sct W78`) — Belirtilen sektördeki aktif trafiği gösterir. Bazı pozisyonların kimliği alt çizgi/sayı içermeyen kısa bir kod da olabilir (örn. `ANKC`).

`.sm vatsim` — Veri kaynağını VATSIM olarak değiştirir.

`.sm rasat` — **Önerilen komut.** Veri kaynağını MGM RASAT olarak değiştirir (**varsayılan**).

`.sm eng` — **Önerilen komut.** Dili İngilizce yapar (**varsayılan**).

`.sm tr` — Dili Türkçe yapar.

`.sm refresh` — METAR verilerini hemen günceller.

`.sm refresh airport` — Trafik listesini hemen günceller.

`.sm ack` — Tüm sarı uyarıları onaylar ve temizler (C butonu da aynı).

`.sm save` — Panel konumu, font boyutu, veri kaynağı ve dil ayarlarını `SMconfig.json` dosyasına kaydeder.

`.sm reload` — Ayarları `SMconfig.json` dosyasından yeniden yükler.

`.sm show` veya `.sm chatbox` — Gizlenmiş chatbox'u tekrar açar.

`.sm help` — Komut listesini görüntüler.

## Panel Butonları

| Buton | İşlev |
|---|---|
| **C** | Tüm sarı uyarıları onaylar (`.sm ack` ile aynı) |
| **-** / **+** | Yazı tipi boyutunu küçültür / büyütür |
| **^** / **v** | Paneli küçültür / genişletir (collapse) |
| Başlık çubuğu | Sürükleyerek panelin ekrandaki konumunu değiştirir |

## Çalışma Mantığı

* Başlangıçta tüm LT* meydanlarının METAR verileri yüklenir.
* Veriler VATSIM veya MGM RASAT üzerinden çekilir (varsayılan kaynak: **RASAT**).
* VATSIM ağı üzerindeki kalkış ve varış trafiği düzenli olarak takip edilir.
* QNH ve rüzgar değişiklikleri zaman bazlı otomatik olarak tespit edilir; sadece QNH değişirse yalnızca QNH sarı, hem QNH hem rüzgar değişirse satırın tamamı sarı gösterilir.
* Yeni tespit edilen trafik kısa süreli olarak mavi bir fontla vurgulanır.
* Filtreleme sayesinde yalnızca istenilen meydan(lar) veya sektöre bağlı trafik görüntülenebilir.
* `.sm online` / `.sm online sct` filtresi, EuroScope sektör dosyasındaki (`.ese`) `[POSITIONS]` ve `[AIRSPACE]` verilerinden hangi pozisyonun hangi meydanlardan sorumlu olduğunu otomatik olarak çıkarır.
* Arayüz dili İngilizce/Türkçe olarak değiştirilebilir (varsayılan: **İngilizce**).
* Panel konumu, yazı boyutu, veri kaynağı ve dil ayarı `SMconfig.json` dosyasında kalıcı olarak saklanır.

---
## Nasıl Yüklenir?
* Öncelikle eklentiyi indiriniz.
* EuroScope'u açtıktan sonra Settings > Plug-ins bölümüne giriniz.
* Load butonuna basarak indirdiğiniz .dll dosyasını seçiniz.
* Eklenti yüklendikten sonra aşağı kaydırınız ve Standart ES Radar Screen üzerinde çizilmesine izin vermek için aşağıdaki görselde kırmızı ile işaretlenen butona basınız.
* Ayarlar penceresini kapattıktan sonra eklenti kullanıma hazır olacaktır.

Gerekli adımlar aşağıdaki görsellerde sırayla gösterilmiştir.

<img width="276" height="450" alt="1" src="https://github.com/user-attachments/assets/562bac05-0ab6-4a5f-89a6-8e3de70a4ae3" />
<img width="727" height="576" alt="2" src="https://github.com/user-attachments/assets/5c9b2422-ab93-4d1a-8b75-18f5944be08e" />
<img width="950" height="782" alt="3" src="https://github.com/user-attachments/assets/16f36ded-0974-4d3d-bc60-06b02f18cad7" />
<img width="937" height="765" alt="4" src="https://github.com/user-attachments/assets/992c8da0-17e7-4c27-ad72-4647ba3f2647" />
<img width="947" height="780" alt="5" src="https://github.com/user-attachments/assets/4d1c61ff-6638-4f6f-a367-39f5b5f0264a" />

made by alp-1863530

Discord: perlanmayer
