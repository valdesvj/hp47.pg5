#!/bin/sh
# run.sh HPTEST ROM - HP47's PC tests on a real 48G/GX ROM (make test). Each prints ok or FAIL; exit 1 on a FAIL.
T=$1; ROM=$2; W=build/test; fail=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }
BOOT="WAIT:3000 KEY:F WAIT:1000"
SUM="KEY:1 KEY:2 KEY:ENTER KEY:3 KEY:ADD KEY:SIN WAIT:500"

# 1. it boots ("Try to recover memory?" NO), sums and takes a sine (in degrees): the screen as test/ref/sum.pbm
$T $ROM $BOOT $SUM SHOT:$W/sum.pbm > $W/run1.txt || bad "boot: $(tail -1 $W/run1.txt)"
cmp -s $W/sum.pbm test/ref/sum.pbm && ok "boot, 12 ENTER 3 + SIN = .258819045103" || bad "the sum's screen (see $W/sum.pbm)"

# 1b. the same through the ROM cache (24 blocks of 2 KB: far smaller than a calculator's), the same screen
$T $ROM -cache 24 $BOOT $SUM SHOT:$W/sumc.pbm > /dev/null 2>&1 && cmp -s $W/sumc.pbm test/ref/sum.pbm &&
  ok "the ROM cache (48 KB): the same screen" || bad "the ROM cache: the sum's screen ($W/sumc.pbm)"

# 2. the state and RAM saved and loaded back: the same screen, and the HP goes on (one more key)
$T $ROM $BOOT $SUM SHOT:$W/a.pbm STATE:$W/st.bin SHOT:$W/b.pbm KEY:COS WAIT:500 SHOT:$W/c.pbm > /dev/null &&
  cmp -s $W/a.pbm $W/b.pbm && ! cmp -s $W/b.pbm $W/c.pbm && ok "state round trip" || bad "state round trip"

# 3. BEEP's pitch at HP_IPS instructions a second (src/main.c): 440 Hz and 2000 Hz within 3 %
IPS=$(sed -n 's/^#define HP_IPS *\([0-9]*\)u.*/\1/p' src/main.c)
B="KEY:SPC KEY:ALPHA KEY:ALPHA KEY:B KEY:E KEY:E KEY:LEFT KEY:ALPHA KEY:ENTER WAIT:3000 BEEPSTAT"
$T $ROM $BOOT BEEPSTAT KEY:4 KEY:4 KEY:0 KEY:SPC KEY:DOT KEY:5 $B KEY:2 KEY:0 KEY:0 KEY:0 KEY:SPC KEY:DOT KEY:2 $B \
  > $W/beep.txt
for want in 440 2000; do
  line=$(grep beeper $W/beep.txt | sed -n "$([ $want = 440 ] && echo 2 || echo 3)p")
  e=$(echo "$line" | awk '{print $2}'); i=$(echo "$line" | awk '{print $5}')
  f=$(awk -v e=$e -v i=$i -v s=$IPS 'BEGIN { if (i > 0) printf "%d", (e - 1) * s / 2 / i; else print 0 }')
  awk -v f=$f -v w=$want 'BEGIN { exit !(f > w * 0.97 && f < w * 1.03) }' && ok "BEEP $want Hz: $f Hz" || bad "BEEP $want Hz: $f Hz"
done
exit $fail
