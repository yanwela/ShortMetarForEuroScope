# ShortMetarForEuroScope

ShortMetar, EuroScope içerisinde Türk meydanlarının METAR bilgilerini hızlı ve kompakt şekilde görüntülemek için geliştirilmiş bir eklentidir. Veriler VATSIM veya MGM RASAT kaynaklarından alınabilir ve aktif trafik bilgileriyle birlikte filtrelenebilir. Panel sürüklenebilir, küçültülebilir, yazı boyutu değiştirilebilir ve ayarlar otomatik olarak saklanabilir.

## Komutlar

`.sm all` — Tüm LT* meydanlarının METAR bilgilerini gösterir.

`.sm used` — Sadece aktif kalkış veya varış trafiği bulunan meydanları gösterir.

`.sm filter LTFM` — İstenilen tek bir meydanı gösterir.

`.sm filter LTFM,LTAW,LTAI` — İstenilen birden fazla meydanı gösterir.

`.sm vatsim` — Veri kaynağını VATSIM olarak değiştirir.

`.sm rasat` — Veri kaynağını MGM RASAT olarak değiştirir.

`.sm refresh` — METAR verilerini hemen günceller.

`.sm refresh airport` — Trafik listesini hemen günceller.

`.sm ack` — Tüm sarı uyarıları onaylar ve temizler (C butonu da aynı).

`.sm save` — Panel konumu, font boyutu ve kaynak ayarlarını `SMconfig.json` dosyasına kaydeder.

`.sm reload` — Ayarları `SMconfig.json` dosyasından yeniden yükler.

`.sm show` veya `.sm chatbox` — Gizlenmiş chatbox'u tekrar açar.

`.sm help` — Komut listesini görüntüler.

## Çalışma Mantığı

* Başlangıçta tüm LT* meydanlarının METAR verileri yüklenir.
* Veriler VATSIM veya MGM RASAT üzerinden çekilir.
* VATSIM ağı üzerindeki kalkış ve varış trafiği düzenli olarak takip edilir.
* QNH ve rüzgar değişiklikleri zaman bazlı otomatik olarak tespit edilir.
* Yeni tespit edilen trafik kısa süreli olarak mavi bir fontla vurgulanır.
* Filtreleme sayesinde yalnızca istenilen meydan(lar) görüntülenebilir.
* Panel konumu, yazı boyutu ve veri kaynağı Smconfig.json sayesinde kalıcı olarak saklanır.

---

made by alp-1863530

Discord: perlanmayer
