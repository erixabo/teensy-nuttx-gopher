# Változások

## 2026-10-29

- 80×24-es, keretezett URL-állapotsor és stabil sorpozíciók.
- Gopher protokollmezők kiszűrése, UTF-8 karaktereket figyelembe vevő igazítás.
- Billentyűzet: nyilak, Tab/Shift-Tab, Enter, PgUp/PgDn, Home/End, `b`, `q`, Ctrl-C.
- Gopher type 7 keresés támogatása.
- Telnet-soros Enter/escape feldolgozás és várakozás javítása.
- Élő Teensy 4.1-en ellenőrizve: menü, helyi szöveges oldal, lapozás, ékezetek,
  visszalépés, kilépés és a Telnet-kapcsolat újranyitása.
- A Telnet nyers módú karakteres navigációjához szükséges NuttX SGA-egyeztetés
  a `teensy4-nuttx` repóban kapott javítást.

# 2026-10-29

- Telnet-kimenet: 20 KiB-os stdout pufferrel a teljes képernyő egy alkalmazás-
  szintű írásban jut a NuttX Telnet driverhez, nem sok 64 bájtos írásban.
- Menüben a kijelölés mozgatásakor csak az előző és az új sort rajzolja újra;
  teljes lapváltáskor és görgetéskor teljes képernyőfrissítés marad.
- Teensy 4.1 build és feltöltés sikeres. Hideg indulás után a Telnet daemon
  autorunból elindult; a főmenü megjelent, a nyíllal léptetés csak két sort
  rajzolt újra, az Enteres lapnyitás működött.
