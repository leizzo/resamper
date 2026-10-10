# Turkish UI glossary

How Resamper's UI reads in Turkish (ADR-0015). Translations go in `UI/translations/tr.txt`; this page decides the words and the tone. When a term is missing, add it here in the same PR that first uses it.

## Tone

- Labels, buttons and menu items are short and neutral: the verb's command form or a noun, no "siz" ("Kaydet", "Geri Al", "Yeni Parça" — never "Kaydedin").
- Dialog sentences and messages address the user as "siz", like macOS in Turkish ("Değişiklikleri kaydetmek istiyor musunuz?").
- Title case is English only: Turkish uses sentence case except for proper nouns and domain terms ("Yeni MIDI Track ekle", not "Yeni MIDI Track Ekle").
- Use the Turkish letters (ı, İ, ş, ğ, ç, ö, ü), never ASCII stand-ins. Upper-casing follows Turkish rules: i → İ, ı → I.

## Kept in English

Domain terms from `CONTEXT.md` keep their English spelling, as Turkish producers know them: Clip, Audio Clip, MIDI Clip, Track, Audio Track, MIDI Track, Send, Return, Bus, Master, Insert, Mixer Insert, Device, Device Chain, Native Device, Plug-in, Rack, Rack Chain, Pad, Macro, Modulator, LFO, Sidechain, Tap, Pre-FX, Pre-Fader, Post-Fader, Strip, Folder, Scene, Slot Clip, Warp, Warp Marker, Transient, Fade, Clip Gain, Take, Loop, Cue, Session View, Arrangement, Mixer, Detail View, Piano Roll, Audio Editor, Automation Lane, Breakpoint, Clip Envelope, Read, Touch, Latch, Write, Sandbox, Theme. Units and abbreviations (dB, Hz, ms, BPM, M/S, Ø) stay as they are.

Device and editing terms that Turkish producers use in English also keep it: Preset, Fader, Velocity, Track Chain, the EQ band types (Low Cut, Low Shelf, Bell, Notch, High Shelf, High Cut) and the Compressor's Attack, Release, Knee, Makeup, Lookahead and Mix. Hardware-style panel captions (A / B, PRE / POST, ST, L+R, ADPT Q, IN GR, LA, DT, RMS, EXP, smp) and note names (C3, F#4) stay as they are.

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
| Enable | Etkinleştir |
| On / Off | Açık / Kapalı |
| Turn On / Turn Off | Aç / Kapat |
| Remove | Kaldır |
| Replace | Değiştir |
| Move | Taşı |
| Locate | Bul |
| Close | Kapat |
| Unpin | Sabitlemeyi kaldır |
| Scan | Tara |
| Library | Kitaplık |
| Search | Ara |
| Empty | Boş |
| Default | Varsayılan |
| Fold / Expand / Compact | Katla / Genişlet / Daralt |
| Colour | Renk |
| Split | Böl |
| Quantize | Kuantize et |
| Reverse / Play Forwards | Ters çevir / İleri çal |
| Bar / Bars (musical) | Ölçü / Ölçüler |
| Start / Length | Başlangıç / Uzunluk |
| Notes (musical) | Notalar |
| Comments | Notlar |
| Armed | Kayda hazır |
| Power | Güç |
| Peak (meter) | Tepe |
| Meter | Ölçer |
| Gain | Kazanç |
| Ratio | Oran |
| Threshold | Eşik |
| Frequency / Freq | Frekans / Frek |
| Band | Bant |
| Scale | Ölçek |
| Out / Output | Çıkış |
| I/O | G/Ç |
| Display | Görünüm |
| Detection | Algılama |
| Activity | Etkinlik |
| Effect | Efekt |
| Parameter | Parametre |
| Editor (a plug-in's) | Düzenleyici |
| Vendor | Üretici |
| Slot | Yuva |
| Loading | Yükleniyor |
| Not loaded (a Failed plug-in) | Başarısız |
| In-process / Out-of-process | Süreç içi / Süreç dışı |
| UI scale | Arayüz ölçeği |
| Sounds / Instruments / Samples | Sesler / Enstrümanlar / Sample'lar |
| Drums / Bass / Chords / Vocal (track colours) | Davul / Bas / Akorlar / Vokal |

"Record" is always "Kayıt", never "Kaydet": "Kaydet" is Save.
