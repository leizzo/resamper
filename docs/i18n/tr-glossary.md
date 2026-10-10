# Turkish UI glossary

How Resamper's UI reads in Turkish (ADR-0015). Translations go in `UI/translations/tr.txt`; this page decides the words and the tone. When a term is missing, add it here in the same PR that first uses it.

## Tone

- Labels, buttons and menu items are short and neutral: the verb's command form or a noun, no "siz" ("Kaydet", "Geri Al", "Yeni Parça" — never "Kaydedin").
- Dialog sentences and messages address the user as "siz", like macOS in Turkish ("Değişiklikleri kaydetmek istiyor musunuz?").
- Title case is English only: Turkish uses sentence case except for proper nouns and domain terms ("Yeni MIDI Track ekle", not "Yeni MIDI Track Ekle").
- Use the Turkish letters (ı, İ, ş, ğ, ç, ö, ü), never ASCII stand-ins. Upper-casing follows Turkish rules: i → İ, ı → I.

## Kept in English

Domain terms from `CONTEXT.md`, and the studio words Turkish producers use as they are (Shaper, Preset, Velocity, Bounce, Tap Tempo), keep their English spelling: Clip, Audio Clip, MIDI Clip, Track, Audio Track, MIDI Track, Send, Return, Bus, Master, Insert, Mixer Insert, Device, Device Chain, Native Device, Plug-in, Rack, Rack Chain, Pad, Macro, Modulator, LFO, Sidechain, Tap, Pre-FX, Pre-Fader, Post-Fader, Strip, Folder, Scene, Slot Clip, Warp, Warp Marker, Transient, Fade, Clip Gain, Take, Loop, Cue, Session View, Arrangement, Mixer, Detail View, Piano Roll, Audio Editor, Automation Lane, Breakpoint, Clip Envelope, Read, Touch, Latch, Write, Sandbox, Theme, Shaper, Preset, Velocity, Bounce, Tap Tempo. Units and abbreviations (dB, Hz, ms, BPM, M/S, Ø) stay as they are.

Turkish suffixes attach to them with an apostrophe: "Clip'i sil", "Track'e ekle", "Send'ler".

## Translated

| English | Türkçe |
|---|---|
| Save | Kaydet |
| Save As… | Farklı Kaydet… |
| Open… | Aç… |
| New Project | Yeni Proje |
| Project | Proje |
| Undo / Redo | Geri Al / Yinele |
| Cut / Copy / Paste | Kes / Kopyala / Yapıştır |
| Delete | Sil |
| Duplicate | Çoğalt |
| Rename | Yeniden Adlandır |
| Cancel / OK | İptal / Tamam |
| Play / Stop / Record | Çal / Durdur / Kayıt |
| Mute / Solo | Sessiz / Solo |
| Arm | Kayda Hazırla |
| Volume / Pan | Ses / Pan |
| Input / Output | Giriş / Çıkış |
| Monitor | İzle |
| Tempo | Tempo |
| Bypass | Devre Dışı |
| Open in Window | Pencerede Aç |
| Pin | Sabitle |
| Reload / Retry | Yeniden Yükle / Tekrar Dene |
| Run in-process | Süreç içinde çalıştır |
| Missing / Failed / Crashed | Eksik / Başarısız / Çöktü |
| Preferences | Tercihler |
| Language | Dil |
| System | Sistem |
| Relaunch now | Şimdi yeniden başlat |
| Later | Sonra |
| Update available | Güncelleme var |
| What's new | Yenilikler |
| File / Edit / Create / View / Options / Help | Dosya / Düzen / Oluştur / Görünüm / Seçenekler / Yardım |
| Menu | Menü |
| Template | Şablon |
| Export | Dışa aktar |
| Recover | Kurtar |
| Autosave | Otomatik kaydet |
| Freeze / Unfreeze | Dondur / Çöz |
| Consolidate | Birleştir |
| Reverse | Ters çevir |
| Split | Böl |
| Note | Nota |
| Quantize | Kuantize et |
| Transpose | Transpoze et |
| Automation | Otomasyon |
| Parameter | Parametre |
| Playhead | Oynatma imleci |
| Time signature | Ölçü rakamı |
| Count-in | Ön sayım |
| Gain | Kazanç (Clip Gain stays English) |
| Scan | Tara |
| Locate | Bul |
| Developer | Geliştirici |
| Song | Şarkı |
| Metronome | Metronom |
| Follow | Takip et |
| Browser | Tarayıcı |
| Zoom In / Zoom Out | Yakınlaştır / Uzaklaştır |
| Selection | Seçim |
| Update / Updated | Güncelle / Güncellendi |
| Update complete | Güncelleme tamamlandı |
| Release notes | Sürüm notları |
| Changelog | Değişiklik günlüğü |
| Restart | Yeniden başlat |
| Downloading / Installing | İndiriliyor / Yükleniyor |

A percentage keeps the English form, `%1%` ("İndiriliyor 40%"), as numbers and units are not localised (ADR-0015).

"Record" is always "Kayıt", never "Kaydet": "Kaydet" is Save ("Record into Arrangement" is "Arrangement'a kayıt yap").

A language's own name ("English", "Türkçe") has an entry equal to its key: it reads the same in every UI Language.
