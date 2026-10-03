# GIN ve hazır çözümler: neyi kullanmadık?

Tarih: 2026-10-03. Kapsam: `external/gin` (commit `ea795541`), Tracktion Engine 3.5.0 ve JUCE 8.0.13'ün sunduğu hazır parçalar ile Resamper'ın kendi yazdığı UI ve plug-in altyapısının karşılaştırması.

## Özet

GIN'den yalnızca `gin::FileSystemWatcher` kullanılıyor (`Source/UI/Developer/LayoutWatcher.cpp`). Bu, bilinçli bir "GIN'e gerek yok" kararı değil: planı kaydeden ADR'ler silindi ve sonraki işler GIN'e bakmadan JUCE ile yürüdü. JSON layout sistemi bugün yalnızca Developer Mode'da görünen tek bir durum çubuğunu (`UI/layouts/statusbar.json`) üretiyor. Plug-in sandbox'ının sıfırdan yazılması ise doğruydu; JUCE, Tracktion ve GIN'de hazır karşılığı yok. Modülasyon ve makrolar için doğru temel `gin_plugin` değil, Tracktion'ın kendi `MacroParameter` ve modifier'ları.

## Nerede kaybolduk

1. **Vertical Slice (#1, `3948827`).** Plan: JSON ile tanımlanan transport çubuğu; GIN modülleri `gin`, `gin_gui`, `gin_svg`, `gin_plugin`. ADR-0007 plug-in arayüzünü JUCE'de tuttu, ADR-0008 bunu tersine çevirdi: "plug-in ve synth arayüzü `gin_plugin` ile yapılır; `gin_dsp`, `gin_graphics`, `gin_simd` derlemeye girer."
2. **Top bar (#19, `9bad5b4`).** Transport C++ ile `TopBar` olarak yeniden yazıldı, `transport.json` silindi. JSON'da yalnızca Developer Mode'a özel `statusbar.json` kaldı.
3. **`78c4af6` "updated design and project mngmnt".** ADR 0001–0012 silindi. `docs/adr` altında bugün yalnızca `0001-realtime-warp-no-proxies.md` var. Kod ve issue'lar (ör. #11) ADR-0004 ve ADR-0006'ya hâlâ atıf yapıyor.
4. **`fe61215`.** Çift derleme hatası düzeltilirken "hiçbir kod kullanmıyor" gerekçesiyle altı GIN modülü kaldırıldı; yalnızca `gin` kaldı.

Kopuş 2. ve 3. adımda: tasarım koda dönüşürken yazılı karar kayboldu, sonraki her iş JUCE ile sıfırdan yapıldı.

## Bizdeki ve hazır karşılığı

| Bizdeki | Hazır karşılığı | Değerlendirme |
|---|---|---|
| `Source/UI/Layout/*` (JSON hstack/vstack, `LayoutManager`, `ComponentFactory`) | `gin::LayoutSupport` (`gin_gui/utilities/gin_layoutsupport.h`) | GIN'inki daha güçlü (ifadeler, sabitler, makrolar, grid, odak sırası) ama başka bir yaklaşım: ifadelerle mutlak konumlandırma. Bizimki tek bir durum çubuğu için yaşıyor; fiilen ölü kod. Transport'un C++'a taşınması, yoğun DAW arayüzünün JSON ile yönetilmediğini gösteriyor; GIN'inkine geçmek sorunu çözmez, sadece taşır. |
| `Source/UI/Developer/Inspector` | `gin::ComponentViewer` (`gin_gui`) ya da topluluğun standart aracı melatonin_inspector | Hazırlar daha kapsamlı: fare altındaki bileşen, hiyerarşi ağacı, özellikler. |
| `Source/UI/Developer/LayoutWatcher` | `gin::FileSystemWatcher` | Kullanılıyor. GIN'den kalan tek parça. |
| `Source/UI/MainWindow/Toasts` | GIN'de yok; JUCE `BubbleMessageComponent` yakın ama yetersiz | Kendimiz yazmamız doğruydu. |
| Özel pencere ve başlık çubuğu | GIN'de yok. JUCE: `DocumentWindow::setUsingNativeTitleBar (false)` ve LookAndFeel ile özel başlık çubuğu | `MainWindow` şu an native başlık çubuğu kullanıyor. Özel pencere JUCE üzerinde kendimiz yapılacak. |
| `Source/UI/Controls/Icons.cpp` (Lucide SVG'leri elle gömülü) | `gin_svg` ya da JUCE `Drawable` | Çalışıyor; değiştirmenin getirisi az. |
| `Source/UI/Mixer/Metering`, `Source/Engine/ClipWaveform` | `gin_dsp`: `LevelMeter`, `WaveformComponent`, `SpectrumAnalyzer`, `DynamicsMeter`, `TriggeredScope` | Bizimkiler ürüne özgü (LUFS, ballistics). Spectrum analizörü ve Ducking Scope (#72) için hazır parçalar var. |
| `Source/UI/Controls/ContinuousControl` (knob, slider) | `gin_plugin`: `Knob`, `MidiLearnOverlay`, `ModMatrix`, `LFOComponent`, `MSEGComponent`, `ADSRComponent`, `PatchBrowser` | Kayıp değil. `gin_plugin` `gin::Processor` ve `gin::Parameter` için tasarlanmış ve kendi görünümüyle geliyor; kullanmak, Tracktion'ın modülasyon sistemine ikinci bir sistem bağlamak olurdu. ADR-0007'nin "JUCE ile kalalım" kararı ADR-0008'den daha doğruydu. |
| Mods Drawer (#83), Macros (#122) için motor tarafı | Tracktion `model/automation`: `MacroParameter`, `MidiLearn`, `Modifier`; `modifiers/` altında `LFOModifier`, `StepModifier`, `EnvelopeFollowerModifier`, `RandomModifier`, `BreakpointOscillatorModifier`, `MIDITrackerModifier` | Doğru temel bu. Edit'e kaydediliyor ve Engine Undo ile çalışıyor; bize yalnızca arayüz kalıyor. |
| `Source/Engine/PluginScanner` (süreç dışı tarama) | Tracktion: `EngineBehaviour::canScanPluginsOutOfProcess`, `tracktion_PluginScanHelpers.h` | Hazırı vardı. Bizimki zaman aşımı ve "Failed to scan" durumu ekliyor; tekrar yazmaya değmez. |
| `Source/Engine/PluginSandbox` (çalışma anında süreç dışı barındırma) | JUCE, Tracktion ve GIN'de yok | Kendimiz yazmamız doğruydu; zaten JUCE `ChildProcessCoordinator` ve `MemoryMappedFile` üzerine kurulu. Tracktion'ın **pluginval** aracı bir sandbox değil, test aracı: CI'da sandbox'ı gerçek plug-in'lerle doğrulamak için kullanılabilir. |

`gin` çekirdek modülünde ileride işe yarayabilecek parçalar: `RealtimeAsyncUpdater`, `LockFreeQueue`, `SharedMemory`, `ValueTreeObject`, `LRUCache`.

## Hazır çözüm kullanmak doğru olur muydu?

Kendi yazdığımız kodun çoğu haklı. Gerçek sorunlar silinen ADR'ler ve kullanılmayan layout sistemi.

- **Evet:** inspector (melatonin_inspector az emekle çok daha fazlasını verir) ve modülasyon ile makrolar için Tracktion'ın modifier'ları.
- **Hayır:** sandbox ve Plug-in Hosting, toast'lar ve pencere görünümü, ürüne özgü metering, `gin_plugin`. Bunlar ya hazır yok, ya ürünün kendisi, ya da Tracktion ile çakışıyor.
- **Asıl hata kod değil, süreç:** ADR'ler silinince kimse "bu kararlaştırılmıştı" diyemedi; `fe61215`'teki modül silme de bunun sonucu. Commit'lerin çoğu agent'larla yazılmış ve agent'lar, söylenmedikçe altyapıyı sıfırdan yazmaya eğilimli.

GIN'den ancak somut bir ihtiyaç doğduğunda tek tek parça alınmalı (ör. spectrum analizörü ya da Ducking Scope #72 için `gin_dsp`). Yeni bir GIN modülü eklenirse AGENTS.md'deki kural geçerli: hem `juce_add_module(...)` hem `resamper_engine` bağlantı listesine eklenir, başka hedeflere eklenmez.

## Önerilen sonraki adımlar

Bu bir özellik işi değil, kod tabanı bakımı. Önce `/improve-codebase-architecture` ile iyileştirme adayları çıkarılmalı, sonra her biri `/grill-with-docs` ile kararlaştırılıp ADR olarak kaydedilmeli. Öncelik sırası:

1. **Kaybolan ADR'ler:** 0001–0012'den hâlâ geçerli olanları `78c4af6^` commit'inden geri getir. ADR-0008'i, ADR-0007'ye dönüş olarak yeniden değerlendir.
2. **Layout sistemi:** Sil (`LayoutManager`, `ComponentFactory`, `Primitives`, `statusbar.json`). Tema hot reload'ı kalsın.
3. **Inspector:** melatonin_inspector'a geç.
4. **Mods Drawer ve Macros (#83, #122):** Tracktion'ın `MacroParameter` ve modifier'ları üzerine kur.
5. **Süreç kuralı:** AGENTS.md'ye ekle: "Yeni bir altyapı parçası yazmadan önce Tracktion, JUCE ve GIN'de karşılığını ara ve bulduğunu PR'da yaz."
