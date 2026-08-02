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

L'**ordine di consumo** dei numeri casuali è parte del contratto: ogni recipe consuma rng() in un ordine fisso documentato dal sorgente del sandbox; cambiarlo cambia tutti i target. La generazione del modello consuma, nell'ordine: eventuale scelta di famiglia (solo se non forzata), corpo della recipe, roll di rarità, budget (un draw sempre consumato), capRoll di complessità, shuffle di Fisher-Yates dei modificatori compatibili, poi i draw interni dei modificatori applicati. Gli errori di stazione usano un generatore separato inizializzato con `atmSeed ^ 0x9e3779b9`; le firme audio con `seed ^ 0x51ab` (non normativo per 10b). Il rumore gaussiano usa Box-Muller sui draw del generatore d'atmosfera.

## 3. Formule normative

**Geometria (frame inerziale geocentrico equatoriale).** Terra: ω_E = 15,0°/h; stazione a (lat φ, lon λ): up = (cosφ·cos(λ+ω_E·t), cosφ·sin(λ+ω_E·t), sinφ), P = R_E·up con R_E = 6 371 000 m. Luna: orbita circolare D = 384 400 km, ω_M = 360°/(27,321661·24 h), inclinazione i_M = 20° sull'equatore, centro C_M(t) = D·(cos a, sin a·cos i_M, sin a·sin i_M) con a = fase₀ + ω_M·t; rotazione sincrona: il frame locale è costruito da e = −Ĉ_M (verso la Terra), k = normale all'orbita, m = k×e; Tycho a (lat −43,3°, lon −11,2°) selenografici, R_M = 1 737 400 m.

**Sorgente e piano uv.** ŝ = (cosδ, 0, sinδ); base uv: ê_u = (0,−1,0), ê_v = (sinδ, 0, −cosδ). Baseline B = P_i − P_j; u = B·ê_u/λ, v = B·ê_v/λ. Campioni temporali: K = 48 punti equispaziati in [−T/2, +T/2] ore (K ridotto se coppie·K > 8000).

**Visibilità.** Un campione è valido se per entrambe le stazioni: elevazione up·ŝ > sin 10° e la linea di vista non è occultata dall'altro corpo (test: proiezione del centro-corpo lungo ŝ positiva e distanza perpendicolare < raggio). Van Cittert–Zernike su griglia: lo spettro del cielo è la FFT 2D centrata (fftshift dell'immagine, FFT, fftshift dello spettro — permutazioni esatte, nessun fattore di fase); il campione V(u,v) è l'interpolazione **bilineare** dello spettro alla coordinata di griglia (N/2 + u·θ_F, N/2 + v·θ_F) con θ_F = FOV in radianti; campioni con coordinate fuori da [1, N−2] sono scartati e contati.

**Corruzione.** Fasi per stazione: serie temporale a 12 modi sinusoidali con ampiezze ∝ m^(−4/3), fasi da rng, normalizzata all'rms richiesto (0 / 0,6 / 1,5 / 3,0 rad); V′ = g_i·g_j·V·e^{i(φ_i−φ_j)}; guadagni g = max(0,3; 1+0,18·N(0,1)); rumore additivo complesso σ = flusso_totale/SNR per componente. Strumento HBT: ampiezza sola (V → |V|, fase 0) e fasi atmosferiche ignorate.

**Gridding e immagini.** Ogni campione (e il suo coniugato hermitiano in (−u,−v)) si accumula nella cella più vicina; pesatura uniforme = divisione per il peso di cella. Beam = IFFT dei pesi, immagine sporca = IFFT delle visibilità grigliate (stessa convenzione di shift), entrambe divise per il picco centrale del beam: l'immagine risulta in unità di flusso/beam (sorgente puntiforme di flusso F → picco F).

**CLEAN di Högbom.** Iterativo: trova il picco assoluto dei residui, sottrai gain·picco·beam traslato, accumula la componente; stop a n iterazioni o picco < 2% del picco iniziale. Restore: componenti convolte con gaussiana di FWHM = clamp(0,9/(r_max·pixel), 2, 24) px sopra i residui. **Il percorso CLEAN non è confrontato matrice-a-matrice** (sensibile all'ordine dei pareggi in virgola mobile): si confrontano gli invarianti (sez. 7).

**Chiusure.** Per il triangolo (a,b,c): Φ = arg(V_ab) + arg(V_bc) − arg(V_ac), avvolta in (−π, π]. Con soli errori di stazione, Φ osservata = Φ vera (test obbligatorio).

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
| dirtyImage, dirtyBeam | number[N²] | matrici normalizzate flusso/beam, 6 cifre |

La batteria (`glasswing_fixture_battery_v1_1.json`) contiene 15 fixture che coprono le 8 famiglie, tutti i sottotipi stellari nuovi, i tre regimi d'array (sito 1–1000 km, rete Terra, Terra-Luna), pesatura naturale e uniforme, e un caso con turbolenza+rumore+guadagni attivi. Nella batteria il CLEAN è disattivato per costruzione (le sue verifiche usano invarianti, non matrici).

## 6. Piano dei test C++

Livello 1 — unitari puri: mulberry32 (sequenze note), generazione modello (stesso seed → stesso JSON), FFT (Parseval, impulso, hermiticità), formule di posizione stazione contro `stationPositionsPerSampleM`. Livello 2 — integrazione: per ogni fixture, ricalcolare (u,v) e visibilità vere dai soli seed+parametri e confrontare con `trueVr/trueVi`; poi con atmSeed applicare la corruzione e confrontare `Vr/Vi`. Livello 3 — golden: gridding+IFFT contro `dirtyImage`/`dirtyBeam`. Livello 4 — invarianti CLEAN: flusso recuperato entro ±10% del valore JS a pari iterazioni, posizione del picco della restored entro 1 px, rms dei residui non superiore a 1,5× quello JS. Livello 5 — proprietà fisiche indipendenti dall'oracolo: chiusure invarianti sotto errori di stazione (< 10⁻⁹ rad senza rumore termico), hermiticità dell'immagine sporca (parte immaginaria ~0), punto uv nullo = flusso totale.

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

## 8. Fuori scope (esplicito)

Termine w e campo largo, smearing di banda e tempo, beam primario, self-calibration completa, SEFD/sensibilità fisica, prestazioni e griglie > 128², effemeridi di precisione (VSOP87 subentra nell'engine: le fixture usano l'orbita circolare dichiarata in `ephemeris`). Questi punti vanno documentati in 10b come limiti noti dell'oracolo, non come bug.
