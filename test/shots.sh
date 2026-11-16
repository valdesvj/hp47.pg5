#!/bin/sh
# shots.sh SIMDIR ROM PYTHON - the PC simulator's screens -> SIMDIR/run/*.png (make shots). Look at every one.
S=$1; ROM=$2; PY=${3:-python3}
rm -rf $S/run && mkdir -p $S/run/simdisk/HP47
cp $ROM $S/run/simdisk/HP47/gxrom-r
cd $S/run || exit 1
# first boot: "Try to recover memory?" NO; a sum and a sine; the equation writer (f 7); the matrix writer (f 9);
# the menu (g .), the key help, about; then off and on again: the HP goes on where it was
timeout 60 ../sim WAIT:2500 SNAP:recover F6 WAIT:1500 SNAP:boot \
    1 2 ENTER 3 ADD SIN WAIT:600 SNAP:sum \
    SHIFT 7 WAIT:800 SNAP:equation EXIT WAIT:600 \
    SHIFT 9 WAIT:800 SNAP:matrix EXIT WAIT:600 \
    SHIFT SHIFT DOT WAIT:300 SNAP:menu 5 WAIT:200 SNAP:keyhelp DOWN WAIT:200 SNAP:keyhelp2 EXIT WAIT:200 8 WAIT:200 \
    SNAP:about EXIT WAIT:200 EXIT WAIT:400 SNAP:back SHIFT SHIFT DOT WAIT:200 F6 WAIT:500 || exit 1
test -f simdisk/HP47/HP47.STA || { echo "shots: the menu's OFF did not save the state"; exit 1; }
timeout 60 ../sim WAIT:1500 SNAP:resumed || exit 1
for n in recover boot sum equation matrix menu keyhelp keyhelp2 about back resumed; do
  $PY ../../../test/sim/render.py $n $n.png || exit 1
done
echo "shots: $(pwd)/*.png"
