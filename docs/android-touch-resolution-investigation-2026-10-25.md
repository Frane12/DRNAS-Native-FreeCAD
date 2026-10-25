# Qt, FreeCAD i dodir: istraživanje i test 25. 10. 2026.

Polazni kod: `8333e9b`, grana `feature/full-freecad-android`.
Upstream FreeCAD: `d4ffc7f725973e073907746e88d320d3e438fd34`.
Android Qt: 6.8.3. Lokalne regresijske provjere: desktop Qt 6.4.2.

## Što je potvrđeno u kodu

**Toolbar i navigacija u modelu imaju različite ulaze.** New je izvorni
`QToolButton` povezan s `QAction`. Ne prolazi kroz Coinov mouse/trackpad
navigacijski stil. Promjena CAD navigacijskog stila zato ne popravlja propušteni
New. Naš Android sloj mora pravilno dostaviti klik tom stvarnom Qt gumbu.

**Qt već usklađuje prikaz i ulaz.** Androidov `androidjniinput.cpp` pretvara
lokalni dodir površine u poziciju native prozora i šalje ga kroz
`QWindowSystemInterface::handleTouchEvent`. Na razini Qt događaja i widgeta
koordinate su logičke. Naš hit-test zato koristi `globalPosition()` i
`mapFromGlobal()`. Ne množi dodir s `devicePixelRatio()`.

**FreeCAD zasebno pretvara poziciju za Coin.** U
`src/Gui/Quarter/EventFilter.cpp`, `trackPointerPosition()` uzima
`QMouseEvent::position()` i poziva `InputDevice::toDevicePixelPosition()`.
`src/Gui/Quarter/InputDevice.cpp` množi x s DPR-om i preokreće y pomoću visine
prozora prije množenja DPR-om. To je mjesto za konverziju prema renderu.
Još jedna takva konverzija u našem touch sloju udvostručila bi skaliranje.

**1024 × 768 je bio premalen na mobitelu.** `StartupActivity.java` je skalu
računao iz kratke fizičke stranice i tražene visine 768, prije učitavanja Qt-a.
Uz to smo smanjivali font na 80% i ikone na 18 logičkih piksela. Dakle,
nedovoljno veliki gumbi bili su posljedica i našeg izbora skale i našeg
kompaktnog izgleda.

**Završno podešavanje izvornog toolbara preskakalo se u desktop načinu.**
Kod je radio `qobject_cast<QMainWindow*>(centralWidget())`, ali je centralni
widget tada `DrnasDesktopWorkspace`. Rezultat je bio null pa završno
podešavanje i ono odgođeno nakon `restoreState()` nisu dobili FreeCAD prozor.
Sada pronalazi ugrađeni `QMainWindow` unutar omotača.

**UI dodir nije imao punu zaštitu od prekida i ponavljanja.** Stari
`DrnasTouchInput` nije čistio UI touch ID pri deaktiviranju aplikacije ili
promjeni veličine hosta. Nije imao vremenski prag za ponovljeni UI tap.
Filtriranje dodatnog sintetiziranog mouse događaja odnosilo se na CAD viewport,
ali ne i na izvorne toolbar gumbe. Novi test je na starom kodu reproducirao
dvostruko pokretanje New za dva neposredna dodira.

To su potvrđeni propusti. Bez novog testa na telefonu nije dokazano da oni
objašnjavaju baš svaki dosadašnji slučaj neaktivnog toolbara.

## Novi profil prikaza

| Profil | Namjena | Veličina istog elementa prema prethodnih 1024 × 768 |
| --- | --- | --- |
| 800 × 600 | Početni odabir nakon ove nadogradnje | približno 1,28 puta veća |
| 640 × 480 | Još jedna niža stepenica | približno 1,60 puta veća |
| 1024 × 768 | Usporedba s prethodnim testom | prethodna skala |

Omjeri vrijede za isti logički element, isti ekran i iste sistemske rubove.
Dodatno povećavamo izvorne toolbar ikone na 24 i stvarne gumbe na najmanje
36 × 36 logičkih jedinica te u tim profilima zadržavamo izvorni font.

`QT_SCALE_FACTOR` se postavlja prije pokretanja Qt-a. Profili mijenjaju Qt-ovu
logičku gustoću; fizička rezolucija Android zaslona ostaje pod kontrolom sustava.
4:3 FreeCAD prozor ostaje u istoj QtActivity površini, centriran. Ako Android
rezervira prostor za sistemske trake ili tipkovnicu, stvarni CAD prozor može
biti manji od nominalnog profila. Konačna geometrija i DPR zapisuju se u log.
Sve aktivnosti za ovaj test koriste vodoravnu orijentaciju.

## Jedan dodir, jedna naredba

1. Native `QWindow` primi touch. Obrađujemo samo našu površinu i pripadajuće
   widgete/prozore, a gumb tražimo na stvarnoj globalnoj Qt poziciji.
2. Zadržavamo widget i ID prsta od početka do završetka. Mali pomak prsta ostaje
   tap; povlačenje izvan gumba ili drugi prst otkazuju UI klik. Drugi prst
   postojeće CAD geste ne može započeti zasebni toolbar klik.
3. Nakon valjanog otpuštanja u red se zajedno stavljaju jedan press i jedan
   release izvornom gumbu. Njegovi QAction, popup i checkable mehanizmi ostaju
   Qt-ovi. To također izbjegava ulazak u `QMenu::exec()` usred touch dispatcha.
4. Prije UI klika više ne šaljemo nepotreban hover/MouseMove, koji može aktivirati
   desktop overlay ponašanje.
5. `QElapsedTimer` mjeri razmak: ponavljanje istog kontrolnog widgeta unutar
   **120 ms** se odbacuje. Različita naredba može se koristiti odmah.
   Postavka **Geste → Zaštita ponovnog klika** dopušta 0–400 ms. Nema `sleep()`
   ni blokiranja event loopa; prvi klik ne čeka tih 120 ms.
6. Dodatni mouse događaji nastali iz istog dodira odbacuju se tijekom sekvence
   i do **250 ms** nakon nje, uz provjeru pozicije i ciljanog kontrolnog widgeta.
   To nije globalna zabrana mišjih događaja. Pravi miš i stock Qt kontrole koje
   nisu preuzete našim raw touch ulazom zadržavaju redovni unos.
7. TouchCancel, skrivanje/deaktiviranje, promjena veličine i modalni prijelazi
   čiste pending UI dodir. Svježi dodir nakon povratka može početi novu sekvencu.

Debounce sprječava ponovno aktiviranje naredbe. Ne može sam riješiti pogrešan
native prozor, odbačene događaje ili svako vizualno treperenje rendera.

## Provjere i log za sljedeći test

Lokalno prolaze pet Qt testnih programa: direct-touch, toolbar-touch,
desktop-workspace, portrait-modal i surface-lifecycle. Toolbar provjera koristi
native QWindow ulaz, a ne izravni poziv QAction. Pokriva New, mali pomak prsta,
brzo ponavljanje, drugu naredbu tijekom debouncea, dodatne Qt/system/touch-device
mouse događaje, pravi miš, otkazivanje, deaktiviranje/povratak i klizanje izvan
gumba. Provjerava se i na DPR 2, 3 i neintegralnoj skali 1,5.

Profili 640 × 480, 800 × 600 i 1024 × 768 provjeravaju centriranje i dodir nakon
promjene veličine. Postojeće provjere modalnih prozora, CAD gesta, runtime
pakiranja i prvog uokvirivanja objekta ostaju uključene.

Na telefonu prvo odabrati 800 × 600 i pritisnuti izvorni New, otvoriti/zatvoriti
dijalog pa ponoviti New. Zatim usporediti 640 × 480. Za problematičan dodir log
sada sadrži `INPUT HIT` s receiverom, globalnom i host koordinatom, DPR-om i
pogođenom klasom; `INPUT UI touch click` dodaje lokalnu koordinatu i tekst gumba;
`INPUT UI debounce` pokazuje odbačeno ponavljanje i izmjeren broj ms.

Uspješan desktop test ili APK build nije potvrda rada na stvarnom Android uređaju.

## Primarni izvori

- [Qt 6.8 High DPI](https://doc.qt.io/qt-6.8/highdpi.html)
- [Qt 6.8 QTouchEvent](https://doc.qt.io/qt-6.8/qtouchevent.html)
- [Qt 6.8.3 Android JNI input](https://github.com/qt/qtbase/blob/v6.8.3/src/plugins/platforms/android/androidjniinput.cpp)
- [Qt 6.8.3 QWidgetWindow touch dispatch](https://github.com/qt/qtbase/blob/v6.8.3/src/widgets/kernel/qwidgetwindow.cpp)
- [FreeCAD Quarter EventFilter](https://github.com/FreeCAD/FreeCAD/blob/d4ffc7f725973e073907746e88d320d3e438fd34/src/Gui/Quarter/EventFilter.cpp)
- [FreeCAD Quarter InputDevice](https://github.com/FreeCAD/FreeCAD/blob/d4ffc7f725973e073907746e88d320d3e438fd34/src/Gui/Quarter/InputDevice.cpp)
