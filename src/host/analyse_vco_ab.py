"""Spectrum check of render_vco_ab.c's single tones (its TONES_DIR): per wave and pitch, H2..H8 against H1 for
Foundations (F) and VCO (A), the energy above 11 kHz and the non-harmonic (alias + interpolation) energy.

  python src/host/analyse_vco_ab.py TONES_DIR
"""
import sys, wave, numpy as np
d=sys.argv[1]
def load(n):
    w=wave.open(f"{d}/{n}.wav"); x=np.frombuffer(w.readframes(w.getnframes()),'<i2').astype(float); return x[4410:4410+32768]
names=["TRI","SAW","SQ","P35","P20","P10","PMIN","SAW+SQ"]
for hz in (110,880,2093):
    print(f"--- {hz} Hz: H2..H8 rel H1 (F / A); harmonics>Nyq/2 energy dB; alias (non-harmonic) dB re total")
    for f in range(8):
        r=[]
        for t in "FA":
            x=load(f"{t}_{hz}_{f}"); X=np.abs(np.fft.rfft(x*np.hanning(len(x))))**2
            fr=np.fft.rfftfreq(len(x),1/44100)
            k=np.round(fr/hz); harm=(np.abs(fr-k*hz)<12)&(k>=1)
            hm=[X[np.abs(fr-h*hz)<6].sum() for h in range(1,9)]
            rel=10*np.log10(np.array(hm[1:])/hm[0]+1e-20)
            tot=X.sum(); al=10*np.log10(X[~harm & (fr>20)].sum()/tot+1e-20)
            hi=10*np.log10(X[harm&(fr>11025)].sum()/tot+1e-20)
            r.append((rel,al,hi))
        (rf,af,hf),(ra,aa,ha)=r
        print(f"{names[f]:7} F "+" ".join(f"{v:5.1f}" for v in rf)+f" | hi {hf:6.1f} alias {af:6.1f}")
        print(f"{'':7} A "+" ".join(f"{v:5.1f}" for v in ra)+f" | hi {ha:6.1f} alias {aa:6.1f}")
