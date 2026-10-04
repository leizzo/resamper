# Resamper

[English](README.md) · **Türkçe**

[![Sürüm derlemesi](https://img.shields.io/github/actions/workflow/status/leizzo/resamper/release.yml?label=s%C3%BCr%C3%BCm%20derlemesi)](https://github.com/leizzo/resamper/actions/workflows/release.yml)
[![Commit mesajları](https://img.shields.io/github/actions/workflow/status/leizzo/resamper/commits.yml?label=commit%20mesajlar%C4%B1)](https://github.com/leizzo/resamper/actions/workflows/commits.yml)
[![Sürüm](https://img.shields.io/github/v/release/leizzo/resamper?include_prereleases&label=s%C3%BCr%C3%BCm)](https://github.com/leizzo/resamper/releases)
[![Destekçiler](https://img.shields.io/github/sponsors/leizzo?label=destek%C3%A7iler)](https://github.com/sponsors/leizzo)

<p align="center">
  <img src="docs/images/hero.png" alt="Resamper — ses tasarımı rack'lerde, miks gerçek bir konsolda. Sidechain kaynak seçici ve send editörüyle arrangement görünümü." width="100%">
</p>

Elektronik müzik prodüktörleri ve miks mühendisleri için koyu temalı, yoğun ve klavye dostu bir
masaüstü DAW.

> **Durum: alfa.** Resamper erken geliştirme aşamasında. Güncel sürüm
> [v0.2.2 — M1.1 Devices & Plug-ins](https://github.com/leizzo/resamper/releases/tag/v0.2.2),
> alfa ön sürümü olarak yayımlandı. Eksik özellikler, pürüzler ve proje formatında değişiklikler
> olabilir — şarkınızın tek kopyasını henüz ona emanet etmeyin.

## Resamper nedir?

Resamper, fikirden yapıya, yapıdan mikse tek bir pencerede ilerlemenizi sağlar. Temel ilkesi:
**ses tasarımı cihaz zincirinde, miks mikserde yapılır** — iki ayrı zincir ve her zaman görebildiğiniz
bir sinyal yolu.

## Tasarım önizlemesi

> Bu görseller ürün tasarımından ([`design/design.pen`](design/design.pen)) alınmıştır ve
> Resamper'ın nereye gittiğini gösterir. İçlerindeki bazı özellikler sonraki kilometre taşlarına
> aittir — v0.2.2'da bugün neler olduğunu görmek için [yol haritasına](#yol-haritası) bakın.

### Liste değil, konsol

<img src="docs/images/mixer.png" alt="Resamper mikseri: rack zinciri bağlantısı, zincir sonrası mikser insert'leri, FX / PRE / POST send'ler, send pan ve faz, A–D return kanalları ve loudness ölçümlü master." width="100%">

### Yerleşik cihazlar kartta. Eklentiler kendi penceresinde.

<img src="docs/images/devices.png" alt="Kart üzerinde düzenlenen yerleşik EQ Eight ve Compressor, kendi penceresinde açılan üçüncü parti eklenti ve kick ile sidechain'lenen kompresör." width="100%">

### İlk döngüden son bounce'a

<img src="docs/images/workflow.png" alt="Gam destekli piano roll, kendi döngüsüne sahip ses klibi zarfları, klasörler ve bus kanalları, arrangement otomasyonu." width="100%">

## Bugün neler yapabilirsiniz (v0.2.2)

- **Aranje** — ses ve MIDI kanalları; taşıyabildiğiniz, boyutlandırabildiğiniz, bölebildiğiniz,
  çoğaltabildiğiniz, döngüyle uzatabildiğiniz ve birleştirebildiğiniz klipler; zoom, kanal yüksekliği
  ve Follow.
- **Kayıt** — giriş seçimi, canlı dalga formu ve take'lerle ses kaydı; MIDI girişinden MIDI kaydı;
  count-in (atlamak için Rec'e Shift ile tıklayın).
- **MIDI düzenleme** — piano roll'da nota düzenleme ve quantize.
- **Ses tasarımı** — detay görünümünde her kanal için cihaz zinciri: grafikleri üzerinden düzenlenen
  EQ Eight ve Compressor, kendi pencerelerinde açılan VST3 / AU / CLAP eklentileri.
- **Güvenli eklenti** — her eklenti kendi sürecinde çalışır: çöken eklenti yalnızca kendi cihazını
  devre dışı bırakır, **Reload** onu geri getirir; taranamayan bir eklenti yeniden denenebilir.
- **Miks** — ses seviyesi, pan, mute, solo, stereo metreler; kanal başına bypass ve sıralama destekli
  8 insert slotu; kendi insert'leri ve Send'leri olan Bus kanalları.
- **Kütüphane** — arama, kategoriler, sample önizleme ve kanala sürükle-bırak içeren kütüphane tarayıcısı.
- **Güvenlik** — otomatik kayıt ve çökme sonrası kurtarma.

Tam liste için [CHANGELOG.md](CHANGELOG.md) dosyasına bakın.

## Yol haritası

| Kilometre taşı | Getirdikleri |
|---|---|
| **M1 — Core** ✅ | Kabuk, transport, Arrangement, eklentili cihaz zinciri, temel Mixer, tasarım sistemi (v0.1.0) |
| **M1.1 — Cihazlar ve Eklentiler** ✅ | Yerleşik cihazlar (EQ Eight, Compressor), çökme izolasyonlu VST3 / AU / CLAP desteği, eklenince açılan eklenti penceresi (v0.2.2) |
| **M2 — Miks** | Pre-FX / Pre / Post send'ler, return kanalları, master loudness, klasörler ve bus'lar, sidechain girişleri |
| **M3 — Otomasyon** | Arrangement otomasyon şeritleri, klip üzeri zarflar, Read / Touch / Latch / Write |
| **M4 — Editörler** | Gam ve akor destekli, velocity şeritli piano roll; warp ve fade destekli ses editörü; klip zarfları |
| **M5 — Rack'ler ve Session** | Instrument / Drum / Audio Effect rack'leri, makrolar, sahneli Session görünümü, crossfader |

Hedef platformlar: macOS 13+ (Apple Silicon) ve Windows 11 x64. Alfa sürümü şu an macOS'ta derleniyor.

## Başlarken

[Releases](https://github.com/leizzo/resamper/releases) sayfasından `Resamper-<sürüm>-macOS.dmg`
dosyasını (Apple Silicon) indirip açın ve Resamper'ı Applications'a sürükleyin. Ya da kaynaktan derleyin
([Geliştiriciler için](README.md#for-developers) bölümüne bakın).

### Klavye kısayolları

`Mod` = macOS'ta Cmd, Windows'ta Ctrl.

| İşlem | Kısayol |
|---|---|
| Çal / Durdur · Seçimden çal | `Space` · `Shift+Space` |
| Kayıt | `F9` (count-in ile; atlamak için Rec'e Shift ile tıklayın) |
| Seçimi döngüye al · Başa dön | `Mod+L` · `Home` |
| Metronom · Tap tempo | `C` · `T` |
| Session ↔ Arrange · Mixer | `Tab` · `Mod+Alt+M` |
| Detay görünümü · tarayıcıyı aç/kapa | `Mod+Alt+L` · `Mod+Alt+B` |
| Geri al · Yinele | `Mod+Z` · `Mod+Shift+Z` |
| Çoğalt · Böl · Birleştir | `Mod+D` · `Mod+E` · `Mod+J` |
| Yeni ses · MIDI kanalı · return | `Mod+T` · `Mod+Shift+T` · `Mod+Alt+T` |
| 1–8. kanalı sustur · Seçiliyi solo | `F1`–`F8` · `S` |
| Yakınlaş / uzaklaş · seçime · şarkıya | `+` / `−` · `Z` · `Shift+Z` |
| Piano roll: quantize · transpoze | `Q` · `↑↓` (yarım ses), `Shift+↑↓` (oktav) |
| Yeni · Aç · Projeyi kaydet | `Mod+N` · `Mod+O` · `Mod+S` |
| Miksi dışa aktar | `Mod+Shift+E` |
| Eklenti pencereleri: odaktakini kapat · tümünü göster / gizle | `Esc` veya `Mod+W` · `Mod+Alt+P` |

## Geri bildirim

Bir hata mı buldunuz ya da bir fikriniz mi var? [Issue açın](https://github.com/leizzo/resamper/issues).

Resamper işinize yarıyorsa [GitHub üzerinden destek olabilirsiniz](https://github.com/sponsors/leizzo).

### 🍺 Beer

Ayda 20 $, ya da bir kez 25 $. İsimler buraya, bir de sürüm notuna düşer.

### ☕️ Coffee

Ayda 5 $, ya da bir kez 10 $. İsimler buraya düşer.

## Lisans

Resamper'ın kendi kaynak kodu [MIT Lisansı](LICENSE) ile yayımlanmıştır. Bu izin yalnızca o dosyaları kapsar.

Uygulamanın derlenmiş hali ayrıca [JUCE](https://juce.com/legal/juce-8-licence/) (AGPLv3 ya da ticari JUCE lisansı) ve [Tracktion Engine](https://engine.tracktion.com/agreement) (GPLv3 ya da ticari Tracktion lisansı) içerir. GIN, BSD-3-Clause lisanslıdır. MIT lisansı JUCE'u veya Tracktion'ı devretmez.

Uygulamayı çalıştırmak serbesttir; ücretli iş buna dahildir. Bir derlemeyi, dağıtım AGPL ve GPL altında kaldığında ve karşılık gelen kaynak sunulduğunda paylaşabilir ve satabilirsiniz. Kapalı kaynak bir derleme için kendi JUCE lisansınız ve kendi Tracktion Engine lisansınız gerekir. Her biri kendi satıcısından alınır; biri diğerini kapsamaz. İkisi de satıştan önce edinilebilir.

Bu, o metinlerin özetidir; hukuki görüş değildir.
