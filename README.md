# NuttX Gopher kliens

Kis, interaktív Gopher kliens NuttX NSH-hoz. A kliens a firmware részeként
fordul le és fut; a Gopher oldalakat hálózaton kéri le. Az SD-kártyáról nem
indul, és nincs szüksége SD-re.

Alapértelmezett kezdőlap: `gopher://138.3.244.113:70/`. A `gopher <selector>`
paranccsal más selector is megadható.

## Képernyő és vezérlés

Alapból a Telnet terminál által közölt méretet használja (NAWS), soros
konzolon vagy méretadat nélkül 80×24 karakterre áll vissza. A méretet
40×12 és 160×60 közé korlátozza a kliens memóriakerete. A `v` helyi nézetmenü
automatikus, 80×24, 100×30 és 120×40 elrendezést kínál; a választás csak az
aktuális futásra szól. A fix méret a kliens tördelését állítja, a távoli
terminálablakot nem tudja átméretezni. Az új mérethez az aktuális Gopher-oldalt
újra lekéri.

A felső sor keretben mutatja az aktuális URL-t. A menüben fel/le nyíl vagy Tab/Shift-Tab léptet, Enter vagy jobbra nyíl
megnyitja a kijelölt elemet. Balra nyíl vagy `b` visszalép, `q` vagy Ctrl-C
kilép. Hosszú menü és szöveges oldal PgUp/PgDn billentyűkkel lapozható; Home/End
a menü elejére/végére ugrik. A Gopher type 7 kereséshez külön beviteli mező van.

A képernyő kimenete 20 KiB-os alkalmazásszintű pufferbe gyűlik, mielőtt a
Telnet drivernek átadja; ez kiváltja a NuttX alapértelmezett 64 bájtos stdio
pufferéből adódó sok apró írást. A menüben a kijelölés mozgatása csak az előző
és az új sort rajzolja újra. Lapváltáskor és görgetéskor továbbra is teljes
képernyőfrissítés történik.

Az információs sorok protokollmezői (`0`/`1`, `null.host`, port) nem jelennek
meg. A szöveg levágása és kitöltése UTF-8 karakterhatáron történik, így az
ékezetes karakterek nem csúsztatják el az oszlopokat. Kilépéskor a kliens
visszaállítja a terminál beállításait.

## Fordítás NuttX alkalmazásként

Másold ezt a könyvtárat az `apps/netutils/gopher` helyre az Apache NuttX Apps
fában, majd válaszd ki a `CONFIG_NETUTILS_GOPHER` opciót. A Teensy 4.1
`netnsh-4.1` konfiguráció integrációját és rögzített upstream verzióit a
[`teensy4-nuttx`](https://github.com/erixabo/teensy4-nuttx) projekt írja le.

Követelmények: IPv4 TCP és DNS (`NET`, `NET_TCP`, `LIBC_NETDB`). A kliens az
NSH előterében fut, mert közvetlenül olvassa a billentyűzetet.

Soros konzolon és Telneten is karakterenkénti bevitelt használ. Ehhez a NuttX
Telnet driver nyers terminálmódú `SUPPRESS-GO-AHEAD` egyeztetése szükséges; a
Teensy buildhez ezt a driverjavítást a `teensy4-nuttx` repó tartalmazza. Enélkül
a Telnet kliens sorpufferelése miatt a nyilak csak Enter után jutnak el az
alkalmazáshoz.

## Teensy 4.1 feltöltése

A firmware-ben lévő USB CDC hook 134 baud beállításakor HalfKay módba kéri a
Teensyt. A `flash-teensy41.sh` a `teensy_loader_cli` programot használja, és
egyszer újrapróbálkozik az első sikertelen loader után. Használat:

```sh
./flash-teensy41.sh firmware.hex
```

A Teensy 4.1 NuttX környezeten tesztelt feltöltés Diga gépen történt. Ha a
firmware-ben még nincs CDC HalfKay hook, a fizikai Program gomb szükséges.

## Korlátok és jogi helyzet

Ez Gopher protokollhoz készült szöveges kliens: TLS-t, HTML-t és bináris
letöltést nem támogat. A kliens külön tárolt forrására ez a repó jelenleg nem
adott külön licencet. A NuttX és a NuttX Apps saját licence változatlanul
Apache-2.0.
