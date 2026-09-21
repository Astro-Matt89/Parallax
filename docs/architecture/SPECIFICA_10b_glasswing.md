# PARALLAX — Specifica di verifica Sprint 10b (Interferometria Glasswing)

Versione 1.1 · generata dal sandbox `glasswing-sandbox-v1.1.html` · da usare come base per la documentazione di sprint con Copilot.

## 1. Scopo e principio

Il sandbox JavaScript è l'**oracolo di riferimento** per il nucleo interferometrico di Parallax 10b. Il contratto è semplice: dati gli stessi input (seed del target, configurazione dell'osservazione, effemeridi), l'implementazione C++ deve produrre gli stessi numeri entro le tolleranze della sezione 7. Tutto ciò che segue è normativo per il nucleo di calcolo; l'integrazione con l'engine (rendering 3D, UI, gameplay) è fuori da questa specifica.

La pipeline normativa è: modello del target (compositivo, deterministico da seed) → rendering del cielo su griglia 128² → FFT del cielo → geometria da effemeridi → campionamento delle visibilità nei punti (u,v) → corruzione strumentale (fasi Kolmogorov, guadagni, rumore termico) → gridding con pesatura → beam sporco e immagine sporca via IFFT → CLEAN di Högbom → fasi di chiusura.

## 2. Contratto RNG (vincolante)

Tutta la generazione procedurale dipende da **mulberry32**, che va portato in C++ **bit-esatto** (aritmetica a 32 bit senza segno, `Math.imul` = moltiplicazione 32-bit con troncamento):

```js
function mulberry32(a){return function(){
  a|=0; a=a+0x6D2B79F5|0;
  let t=Math.imul(a^a>>>15,1|a);
  t=t+Math.imul(t^t>>>7,61|t)^t;
  return ((t^t>>>14)>>>0)/4294967296;
}}
```

L'**ordine di consumo** dei numeri casuali è parte del contratto: ogni recipe consuma rng() in un ordine fisso documentato dal sorgente del sandbox; cambiarlo cambia tutti i target. La generazione del modello consuma, nell'ordine: eventuale scelta di famiglia (solo se non forzata), corpo della recipe, roll di rarità, budget (un draw sempre consumato), capRoll di complessità, shuffle di Fisher-Yates dei modificatori compatibili, poi i draw interni dei modificatori applicati. Gli errori di stazione usano un generatore separato inizializzato con `atmSeed` così com'è: `mulberry32(atmSeed)`, **senza xor** (le versioni precedenti di questa specifica indicavano `atmSeed ^ 0x9e3779b9`, in contraddizione con l'oracolo: vince l'oracolo); le firme audio con `seed ^ 0x51ab` (non normativo per 10b). Il rumore gaussiano usa Box-Muller sui draw del generatore d'atmosfera (un draw nullo viene scartato e ripetuto, come nei cicli `while(u===0)` dell'oracolo). Sul generatore d'errore l'ordine è: per ogni stazione la serie `kolmSeries` (nessun draw se rms ≤ 0; con K = 1 un solo `randn`·rms; altrimenti 12 fasi), poi un guadagno per stazione (se attivi), poi il rumore termico (`Vr` poi `Vi`) per ogni campione accettato, iterando il tempo k all'esterno e le coppie i < j all'interno. Le stazioni sono quelle già ridotte per strumento: HBT e comb tengono solo le stazioni terrestri **prima** di formare le coppie, quindi coppie, cap di K e draw per stazione seguono la lista ridotta.

## 3. Formule normative

**Geometria (frame inerziale geocentrico equatoriale).** Terra: ω_E = 15,0°/h; stazione a (lat φ, lon λ): up = (cosφ·cos(λ+ω_E·t), cosφ·sin(λ+ω_E·t), sinφ), P = R_E·up con R_E = 6 371 000 m. Luna: orbita circolare D = 384 400 km, ω_M = 360°/(27,321661·24 h), inclinazione i_M = 20° sull'equatore, centro C_M(t) = D·(cos a, sin a·cos i_M, sin a·sin i_M) con a = fase₀ + ω_M·t; rotazione sincrona: il frame locale è costruito da e = −Ĉ_M (verso la Terra), k = normale all'orbita, m = k×e; Tycho a (lat −43,3°, lon −11,2°) selenografici, R_M = 1 737 400 m.

**Sorgente e piano uv.** ŝ = (cosδ, 0, sinδ); base uv: ê_u = (0,−1,0), ê_v = (sinδ, 0, −cosδ). Baseline B = P_i − P_j; u = B·ê_u/λ, v = B·ê_v/λ. Campioni temporali: K = 48 punti equispaziati in [−T/2, +T/2] ore (K ridotto se coppie·K > 8000). Le posizioni delle stazioni si valutano a t = Hs[k]: l'epoca agisce solo sul modello del target (`applyTemporal`), mai sulle effemeridi.

**Visibilità.** Un campione è valido se per entrambe le stazioni: elevazione up·ŝ > sin 10° e la linea di vista non è occultata dall'altro corpo (test: proiezione del centro-corpo lungo ŝ positiva e distanza perpendicolare < raggio). Van Cittert–Zernike su griglia: lo spettro del cielo è la FFT 2D centrata (fftshift dell'immagine, FFT, fftshift dello spettro — permutazioni esatte, nessun fattore di fase); il campione V(u,v) è l'interpolazione **bilineare** dello spettro alla coordinata di griglia (N/2 + u·θ_F, N/2 + v·θ_F) con θ_F = FOV in radianti; campioni con coordinate fuori da [1, N−2] sono scartati e contati.

**Corruzione.** Fasi per stazione: serie temporale a 12 modi sinusoidali con ampiezze ∝ m^(−4/3), fasi da rng, normalizzata all'rms richiesto (0 / 0,6 / 1,5 / 3,0 rad); V′ = g_i·g_j·V·e^{i(φ_i−φ_j)}; guadagni g = max(0,3; 1+0,18·N(0,1)); rumore additivo complesso σ = flusso_totale/SNR per componente. Strumento HBT: ampiezza sola (V → |V|, fase 0) e fasi atmosferiche ignorate.

**Gridding e immagini.** Ogni campione (e il suo coniugato hermitiano in (−u,−v)) si accumula nella cella più vicina; pesatura uniforme = divisione per il peso di cella. Beam = IFFT dei pesi, immagine sporca = IFFT delle visibilità grigliate (stessa convenzione di shift), entrambe divise per il picco centrale del beam: l'immagine risulta in unità di flusso/beam (sorgente puntiforme di flusso F → picco F).

**CLEAN di Högbom.** Iterativo: trova il picco assoluto dei residui, sottrai gain·picco·beam traslato, accumula la componente; stop a n iterazioni o picco < 2% del picco iniziale. Restore: componenti convolte con gaussiana di FWHM = clamp(0,9/(r_max·pixel), 2, 24) px sopra i residui, con σ = FWHM/2,355 (letterale dell'oracolo, non 2·√(2·ln 2)) e finestra quadrata di semilato ⌈3σ⌉ troncata ai bordi della griglia. **Il percorso CLEAN non è confrontato matrice-a-matrice** (sensibile all'ordine dei pareggi in virgola mobile): si confrontano gli invarianti (sez. 7).

**Chiusure.** Per il triangolo (a,b,c): Φ = arg(V_ab) + arg(V_bc) − arg(V_ac), avvolta in (−π, π]. Con soli errori di stazione, Φ osservata = Φ vera (test obbligatorio). **Selezione dei triangoli — divergenza deliberata dall'oracolo.** L'oracolo prende i primi 3 triangoli per indice anche se non hanno alcun istante con tutte e tre le baseline presenti; il C++ prende i primi 3 per indice che hanno almeno un istante coperto. Un triangolo senza copertura dà una serie vuota e nessuna informazione, quindi su questo punto il C++ non riproduce l'oracolo, per scelta. Le due regole coincidono quando i triangoli di indice più basso hanno dati; nessuna fixture esporta le chiusure, quindi la scelta non è verificata dal gate.

## 4. Modello del target

Architettura compositiva: primitive (point, gaussian ellittica, disk con oscuramento al bordo, ring con armoniche azimutali, jet a nodi, planet_surface, absorption) → recipe per famiglia (BINARY, STAR, PROTO_DISK, NOVA, AGN, COMPACT, PLANETARY, PLANET_RES) → modificatori con tabella di compatibilità. Le famiglie STAR e NOVA includono i sottotipi estesi: supergigante, oblata, **nana bruna** (bande atmosferiche rotanti), **T Tauri** (disco interno + getto Herbig-Haro con moto proprio a 60–240 km/s), **Wolf-Rayet** (girandola di polvere rigida, periodo in giorni), **supergigante morente** (gusci staccati in espansione, celle di convezione, maser circumstellari), guscio di nova, **pulsar** (periodo 1,6 ms–2 s, misura di dispersione). Ogni componente porta spectralModel (stellar ν², thermal_dust ν^3,5, synchrotron ν^α per componente, free_free ν^−0,1, maser a campana in log-ν) e il rendering applica fluxScale/sizeScale alla banda corrente; i modelli temporali (orbit, multi_orbit, rotation, planet_rotation, expansion, proper_motion) trasformano le componenti in funzione dell'epoca in giorni. Per 10b il rendering del cielo può essere portato tal quale o sostituito, purché le fixture (che includono il cielo implicito nelle visibilità vere) tornino nei limiti.

## 5. Schema JSON della fixture

| Campo | Tipo | Significato |
|---|---|---|
| version, generator, scenario, subtype | string | provenienza e scenario |
| seed, atmSeed, requestedClass, complexity | number/string | ricostruiscono il modello e gli errori |
| designation, family | string | identificazione |
| lambdaMeters, epochDays, decDeg | number | banda, epoca, declinazione |
| mode, instrument, weighting, nulling | string/bool | modalità array, strumento, pesatura |
| turbulenceRms, snr, gainErrors, rotation, durationHours | number/bool | corruzione e traccia |
| siteScaleM, siteBody, siteLatDeg | number/string | sito (modalità sandbox) |
| sampleTimesHours | number[K] | tempi campione (ore dal centro traccia) |
| ephemeris | object | ω_E, ω_M, D, R_M, i_M, fase₀ Luna, R_E, Tycho lat/lon (rad) |
| stations | {code,kind,latRad,lonRad}[] | descrittori |
| stationPositionsPerSampleM | number[K][nSt][3] | posizioni XYZ per campione (verifica indipendente della geometria) |
| gridN, thetaFovRad, thetaObjRad, fluxTotal | number | griglia e normalizzazioni |
| visibilities | {u,v,Vr,Vi,trueVr,trueVi,k}[] | campioni: corrotti E veri |
| dirtyImage, dirtyBeam | number[N²] | matrici normalizzate sul picco del beam (flusso/beam), a piena precisione (vedi §7) |

La batteria corrente (`glasswing_fixture_battery_v1_4.json`, generata nel browser dall'oracolo v1.8.0) contiene 18 fixture che coprono le 8 famiglie, tutti i sottotipi stellari nuovi, i tre regimi d'array (sito 1–1000 km, rete Terra, Terra-Luna), pesatura naturale e uniforme, un caso con turbolenza+rumore+guadagni attivi e i tre array lunari a Y sulla base Tycho (1, 10 e 100 km), che sono lo strumento del gioco e fino alla v1.3 non erano mai stati esercitati. Le prime 15 fixture della v1.4 sono identiche byte per byte a quelle della v1.3. Nella batteria il CLEAN è disattivato per costruzione (le sue verifiche usano invarianti, non matrici).

## 6. Piano dei test C++

Livello 1 — unitari puri: mulberry32 (sequenze note), generazione modello (stesso seed → stesso JSON), FFT (Parseval, impulso, hermiticità), formule di posizione stazione contro `stationPositionsPerSampleM`. Livello 2 — integrazione: per ogni fixture, ricalcolare (u,v) e visibilità vere dai soli seed+parametri e confrontare con `trueVr/trueVi`; poi con atmSeed applicare la corruzione e confrontare `Vr/Vi`. Livello 3 — golden: gridding+IFFT contro `dirtyImage`/`dirtyBeam`. Livello 4 — invarianti CLEAN: flusso recuperato entro ±10% del valore JS a pari iterazioni, posizione del picco della restored entro 1 px, rms dei residui non superiore a 1,5× quello JS. Livello 5 — proprietà fisiche indipendenti dall'oracolo: chiusure invarianti sotto errori di stazione (< 10⁻⁹ rad senza rumore termico), hermiticità dell'immagine sporca (parte immaginaria ~0), punto uv nullo = flusso totale.

**Occultazione: coperta da test unitario, non dal gate.** Il ramo "vero" di `occultedBy` non è esercitato da nessuna fixture. Una scansione dello spazio dei parametri in modalità full (declinazione da −80° a +80° × durata da 24 a 648 h, con `moonPhase0` = 70° fissata dalla battery) trova un solo caso raggiungibile — la Terra che copre Tycho, finestra di 3,3 h a declinazione 0 contro un passo di campionamento di 13,8 h a 648 h, cioè **un solo campione-stazione** — e nessun caso di Luna che copre una stazione terrestre. Una fixture costruita su quell'unico campione perderebbe la copertura al primo cambio di `moonPhase0` o di K, restando verde: per questo la copertura sta in `tests/test_ephemeris.cpp`, che verifica entrambe le direzioni (Terra fra stazione lunare e sorgente, Luna fra stazione terrestre e sorgente), i casi negativi con il corpo dietro la stazione e i casi al limbo, dentro e fuori di 1e-6 relativo sull'angolo.

## 7. Tolleranze

| Quantità | Tolleranza | Nota |
|---|---|---|
| Posizioni stazione | 10⁻⁶ m assoluti | trigonometria double, chiusa |
| (u,v) | 10⁻⁹ relativi | |
| Visibilità vere | 10⁻⁷ relativi sul modulo | bilineare deterministica |
| Visibilità corrotte | 10⁻⁷ relativi | richiede RNG bit-esatto |
| dirtyImage/dirtyBeam | 10⁻⁶ × picco, per pixel | ordine FFT può differire al bit |
| CLEAN | solo invarianti (sez. 6) | percorso non riproducibile |

Se l'RNG C++ non è bit-esatto, i livelli 2-corrotto e 3 falliscono a catena: verificare il livello 1 per primo, sempre.

**Precisione dell'export (vincolo del contratto).** Le matrici `dirtyImage` e `dirtyBeam` (e `cleanRestored`, quando presente) sono esportate a piena precisione double, senza arrotondamento, come le visibilità. Non è un dettaglio di formato: la tolleranza del livello 3 è assoluta, 10⁻⁶ × picco per pixel, mentre un export a 6 cifre significative introduce da solo un errore fino a 5·10⁻⁶ × picco quando la mantissa dei valori è bassa (la dirty image è normalizzata sul picco del beam, quindi i suoi pixel possono valere diverse unità). Con la battery v1.2, esportata con `toPrecision(6)`, nemmeno l'oracolo superava il proprio export su 11 fixture su 15. Una battery con matrici immagine arrotondate non è valida per il livello 3.

**Parametri fissati dalla battery.** La generazione della battery imposta esplicitamente, e ripristina al termine, ogni parametro che tocca la pipeline senza essere dichiarato per scenario:

| Parametro | Valore | Cosa determina |
|---|---|---|
| atmSeed | 777 | stream degli errori di stazione |
| N (griglia) | 128 | cielo, FFT, gridding |
| iterazioni CLEAN | 0 | nessuna matrice CLEAN nelle fixture |
| latitudine del sito | 28° | stazioni della modalità sandbox |
| epoca | 0 giorni | nessuna evoluzione temporale del target |
| fase lunare (moonPhase0) | 70° | posizioni delle stazioni lunari |
| guadagno di loop del CLEAN | 15 % | ininfluente con 0 iterazioni |
| nulling | disattivato | cielo degli strumenti comb/epr |
| complessità | structured | modificatori del modello |
| stazioni terrestri (GW) | default di fabbrica (La Palma, Mauna Kea, Paranal attive) | rete Terra e modalità mista |
| layout dell'array sandbox | preset Y | stazioni della modalità sandbox |

Una battery generata con valori diversi non è confrontabile con queste fixture: prima della v1.7.7 questi valori venivano ereditati dai cursori dell'interfaccia, e una battery rigenerata con un cursore spostato cambiava stazioni e visibilità senza segnalarlo. Dalla v1.7.10 in avanti (quindi anche nella v1.8.0 corrente) la linea di base è applicata **dentro il ciclo**, prima del `cfg` di ogni scenario (così nessuno scenario eredita dal precedente), e agisce sulle **variabili derivate**, non sui cursori: scrivere `.value` non scatena l'evento del cursore, quindi `resetEpoch()`, l'assegnazione di `moonPhase0`, i default di `GW` e `presets("y")` sono l'unico modo di fissare davvero epoca, fase lunare, stazioni e layout. Verificato headless: perturbando epoca, fase lunare, complessità, stazioni GW e preset dell'array, e invertendo l'ordine degli scenari, la battery resta identica byte per byte. Nota: il cursore `slGain` dell'oracolo (commentato come "ampiezza errori di guadagno") è il guadagno di loop del CLEAN (`cgain`); l'ampiezza degli errori di guadagno delle stazioni non è un parametro, è fissa a 0,18 nel codice (g = max(0,3; 1+0,18·N(0,1)), §3).

**Motore di generazione (vincolo del contratto).** La battery di riferimento si genera **nel browser**, col pulsante dell'oracolo, non in node. L'identità byte per byte vale solo **entro lo stesso motore JavaScript**: motori diversi differiscono sull'ultimo bit delle funzioni matematiche e la differenza si propaga lungo la pipeline. Fra motori diversi vale invece la tolleranza del gate, cioè la tabella qui sopra. Misure sulla v1.3 (oracolo v1.7.10 rieseguito headless in node 18 contro la battery generata nel browser, metriche del gate): posizioni 2,9e-16, (u,v) 2,5e-11, Vr/Vi 3,6e-10, trueVr/trueVi 6,3e-10, dirtyImage 4,5e-13 × picco, dirtyBeam 0. La deriva arriva anche ai parametri dichiarati: nella fixture 13 `lambdaMeters` differisce di 1 ulp fra i due motori. Rimisurato sulla v1.4 (oracolo v1.8.0 headless contro la battery di Chrome 152): stessi ordini di grandezza, e le tre fixture lunari non fanno eccezione — (u,v) 6,9e-10, Vr/Vi 2,4e-10, dirtyImage 2,4e-13 × picco.

Il C++ concorda con la battery del browser allo stesso ordine di grandezza (Vr 3,6e-10, trueVr 6,3e-10 nel gate di livello 2), quindi rigenerare la battery in node **non** la avvicinerebbe al C++: sposterebbe il riferimento di una quantità paragonabile al margine del gate, senza alcun guadagno. Regola: il riferimento si produce nel browser; la rigenerazione headless (`tools/oracle_harness/regenerate_battery.js`) serve a verificare che il risultato non dipenda dall'ordine degli scenari né dallo stato dell'interfaccia, non a produrre il riferimento.

**Motore registrato nell'header.** Dalla v1.8.0 l'oracolo scrive nella battery `oracleVersion`, `userAgent` e `generatedAt`: la provenienza non va più ricostruita né annotata a mano. La v1.4 dichiara oracolo 1.8.0, Chrome 152.0.0.0 su Windows x64, 2026-09-16T09:22:11Z. Per la v1.3 non era stato possibile ricostruirla: il file non porta metadati di download (nessun flusso `Zone.Identifier`) e la battery non li dichiarava.

## 8. Fuori scope (esplicito)

Termine w e campo largo, smearing di banda e tempo, beam primario, self-calibration completa, SEFD/sensibilità fisica, prestazioni e griglie > 128², effemeridi di precisione (VSOP87 subentra nell'engine: le fixture usano l'orbita circolare dichiarata in `ephemeris`). Questi punti vanno documentati in 10b come limiti noti dell'oracolo, non come bug.
