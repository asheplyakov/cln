// print_float().

// General includes.
#include "cl_sysdep.h"

// Specification.
#include "cln/float_io.h"


// Implementation.

// Michael Stoll 10.2.1990 - 26.3.1990
// Bruno Haible 8.9.1990 - 10.9.1990

// Grundgedanken:
// Jede Real-Zahl /= 0 repr�sentiert ein (offenes) Intervall. Es wird die-
// jenige Dezimalzahl mit m�glichst wenig Stellen ausgegeben, die in diesem
// Intervall liegt.
// Um auch gro�e Exponenten zu behandeln, werden Zweier- in Zehnerpotenzen
// erst einmal n�herungsweise umgerechnet. N�tigenfalls wird die Rechen-
// genauigkeit erh�ht. Hierbei wird von den Long-Floats beliebiger
// Genauigkeit Gebrauch gemacht.

// St�tzt sich auf:
// cl_ln2(digits) liefert ln(2) mit mindestens digits Mantissenbits.
// cl_ln10(digits) liefert ln(10) mit mindestens digits Mantissenbits.
// cl_decimal_string(integer) liefert zu einem Integer >0
//   einen String mit seiner Dezimaldarstellung.
// (substring string start [end]) wie subseq, jedoch f�r Strings schneller.

CL_REQUIRE(cl_F_ln2_var)
CL_REQUIRE(cl_F_ln10_var)
#include <cstring>
#include "cln/output.h"
#include "cl_sstring.h"
#include "cln/float.h"
#include "cl_F.h"
#include "cl_LF.h"
#include "cl_F_tran.h"
#include "cln/rational.h"
#include "cln/integer.h"
#include "cln/integer_io.h"
#include "cl_I.h"

namespace cln {

// Hauptfunktion zur Umwandlung von Floats ins Dezimalsystem:
// Zu einem Float x werden ein String as und drei Integers k,e,s
// berechnet mit folgenden Eigenschaften:
// s = sign(x).
// Falls x/=0, betrachte |x| statt x. Also oBdA x>0.
//   Seien x1 und x2 die n�chstkleinere bzw. die n�chstgr��ere Zahl zu x
//   vom selben Floating-Point-Format. Die Zahl x repr�sentiert somit das
//   offene Intervall von (x+x1)/2 bis (x+x2)/2.
//   a ist ein Integer >0, mit genau k Dezimalstellen (k>=1), und es gilt
//   (x+x1)/2 < a*10^(-k+e) < (x+x2)/2 .
//   Dabei ist k minimal, also a nicht durch 10 teilbar.
// Falls x=0: a=0, k=1, e=0.
// as ist die Ziffernfolge von a, der L�nge k.

// typedef
struct cl_decimal_decoded_float {
	char * a;
	uintL k;
	cl_I e;
	cl_I s;
// Constructor.
	cl_decimal_decoded_float (char * ap, uintL kp, const cl_I& ep, const cl_I& sp) : a(ap), k(kp), e(ep), s(sp) {}
};

static const cl_decimal_decoded_float decode_float_decimal (const cl_F& x)
{
  var cl_idecoded_float x_idecoded = integer_decode_float(x);
  var cl_I& binmant = x_idecoded.mantissa;
  var cl_I& binexpo = x_idecoded.exponent;
  var cl_I& sign = x_idecoded.sign;
  if (eq(binmant,0)) // x=0 ?
    // a=0, k=1, e=0, s=0
    return cl_decimal_decoded_float(cl_sstring("0",1), 1, 0, 0);
  // x/=0, also ist sign das Vorzeichen von x und
  // |x| = 2^binexpo * float(binmant,x) . Ab jetzt oBdA x>0.
  // Also x = 2^binexpo * float(binmant,x) .
  var uintL l = integer_length(binmant); // Anzahl der Bits von binmant, >=3
  var cl_I binmant2 = ash(binmant,1); // 2*binmant
  var cl_I oben = plus1(binmant2); // obere Intervallgrenze ist
                                   // (x+x2)/2 = 2^(binexpo-1) * oben
  var cl_I unten = minus1(binmant2); // untere Intervallgrenze ist
  var uintL untenshift = 0;          // (x+x1)/2 = 2^(binexpo-1-untenshift) * unten
  if (integer_length(unten) == l) {
    // Normalerweise integerlength(unten) = 1+integerlength(binmant).
    // Hier integerlength(unten) = l = integerlength(binmant),
    // also war binmant eine Zweierpotenz. In diesem Fall ist die
    // die Toleranz nach oben 1/2 Einheit, aber die Toleranz nach unten
    // nur 1/4 Einheit: (x+x1)/2 = 2^(binexpo-2) * (4*binmant-1)
    unten = minus1(ash(binmant2,1));
    untenshift = 1;
  }
  // Bestimme d (ganz) und a1,a2 (ganz, >0) so, da�
  // die ganzen a mit (x+x1)/2 < 10^d * a < (x+x2)/2 genau
  // die ganzen a mit a1 <= a <= a2 sind und 0 <= a2-a1 < 20 gilt.
  // Wandle dazu 2^e := 2^(binexpo-1) ins Dezimalsystem um.
  var cl_I e = binexpo - 1;
  var bool e_gross = (abs(e) > ash(l,1)); // Ist |e| recht gro�, >2*l ?
  var uintL g;     // Hilfsvariablen f�r den Fall, da� |e| gro� ist
  var cl_I f;      //
  var cl_I zehn_d; // Hilfsvariable 10^|d| f�r den Fall, da� |e| klein ist
  var cl_I d;  // Ergebnisvariablen
  var cl_I a1; //
  var cl_I a2; //
  if (e_gross) { // Ist |e| recht gro� ?
    // Da 2^e nur n�herungsweise gehen kann, braucht man Schutzbits.
    var uintL h = 16; // Anzahl der Schutzbits, mu� >= 3 sein
    neue_schutzbits:
    // Ziel: 2^e ~= 10^d * f/2^g, wobei 1 <= f/2^g < 10.
    g = l + h; // Anzahl der g�ltigen Bits von f
    // Sch�tze d = floor(e*lg(2))
    // mit Hilfe der N�herungsbr�che von lg(2):
    // (0 1/3 3/10 28/93 59/196 146/485 643/2136 4004/13301
    //  8651/28738 12655/42039 21306/70777 76573/254370 97879/325147
    //  1838395/6107016 1936274/6432163 13456039/44699994
    //  15392313/51132157 44240665/146964308 59632978/198096465
    //  103873643/345060773 475127550/1578339557 579001193/1923400330
    // )
    // e>=0 : w�hle lg(2) < a/b < lg(2) + 1/e,
    //        dann ist d <= floor(e*a/b) <= d+1 .
    // e<0  : w�hle lg(2) - 1/abs(e) < a/b < lg(2),
    //        dann ist d <= floor(e*a/b) <= d+1 .
    // Es ist bekannt, dass abs(e) <= 2^32*64 + 2^31 .
    // Unser d sei := floor(e*a/b)-1. (d /= 0, da abs(e) >= 7.)
    d = minus1(minusp(e)
               ? (e >= -970
                  ? floor1(e*3,10) // N�herungsbruch 3/10
                  : floor1(e*97879,325147) // N�herungsbruch 97879/325147
                 )
               : (e <= 22000
                  ? floor1(e*28,93) // N�herungsbruch 28/93
                  : floor1(e*1838395,6107016) // N�herungsbruch 1838395/6107016
                 )
              );
    // Das wahre d wird durch diese Sch�tzung entweder getroffen
    // oder um 1 untersch�tzt.
    // Anders ausgedr�ckt: 0 < e*log(2)-d*log(10) < 2*log(10).
    // Nun f/2^g als exp(e*log(2)-d*log(10)) berechnen.
    // Da f < 100*2^g < 2^(g+7), sind g+7 Bits relative Genauigkeit
    // des Ergebnisses, also g+7 Bits absolute Genauigkeit von
    // e*log(2)-d*log(10) n�tig. Dazu mit l'=integerlength(e)
    // f�r log(2): g+7+l' Bits abs. Gen., g+7+l' Bits rel. Gen.,
    // f�r log(10): g+7+l' Bits abs. Gen., g+7+l'+2 Bist rel. Gen.
    var float_format_t gen = (float_format_t)(g + integer_length(e) + 9); // Genauigkeit
    var cl_F f2g = exp(The(cl_F)(e * cl_ln2(gen)) - The(cl_F)(d * cl_ln10(gen))); // f/2^g
    // Das so berechnete f/2^g ist >1, <100.
    // Mit 2^g multiplizieren und auf eine ganze Zahl runden:
    f = round1(scale_float(f2g,g)); // liefert f
    // Eventuell f und d korrigieren:
    if (f >= ash(10,g)) // f >= 10*2^g ?
      { f = floor1(f,10); d = d+1; }
    // Nun ist 2^e ~= 10^d * f/2^g, wobei 1 <= f/2^g < 10 und
    // f ein Integer ist, der um h�chstens 1 vom wahren Wert abweicht:
    // 10^d * (f-1)/2^g < 2^e < 10^d * (f+1)/2^g
    // Wir verkleinern nun das offene Intervall
    // von (x+x1)/2 = 2^(binexpo-1-untenshift) * unten
    // bis (x+x2)/2 = 2^(binexpo-1) * oben
    // zu einem abgeschlossenen Intervall
    // von 10^d * (f+1)/2^(g+untenshift) * unten
    // bis 10^d * (f-1)/2^g * oben
    // und suchen darin Zahlen der Form 10^d * a mit ganzem a.
    // Wegen  oben - unten/2^untenshift >= 3/2
    // und  oben + unten/2^untenshift <= 4*binmant+1 < 2^(l+2) <= 2^(g-1)
    // ist die Intervall-L�nge
    // = 10^d * ((f-1)*oben - (f+1)*unten/2^untenshift) / 2^g
    // = 10^d * ( f * (oben - unten/2^untenshift)
    //            - (oben + unten/2^untenshift) ) / 2^g
    // >= 10^d * (2^g * 3/2 - 2^(g-1)) / 2^g
    // = 10^d * (3/2 - 2^(-1)) = 10^d
    // und daher gibt es in dem Intervall mindestens eine Zahl
    // dieser Form.
    // Die Zahlen der Form 10^d * a in diesem Intervall sind die
    // mit a1 <= a <= a2, wobei a2 = floor((f-1)*oben/2^g) und
    // a1 = ceiling((f+1)*unten/2^(g+untenshift))
    //    = floor(((f+1)*unten-1)/2^(g+untenshift))+1 .
    // Wir haben eben gesehen, da� a1 <= a2 sein mu�.
    a1 = plus1(ash(minus1((f+1)*unten),-(g+untenshift)));
    a2 = ash((f-1)*oben,-g);
    // Wir k�nnen auch das offene Intervall
    // von (x+x1)/2 = 2^(binexpo-1-untenshift) * unten
    // bis (x+x2)/2 = 2^(binexpo-1) * oben
    // in das (abgeschlossene) Intervall
    // von 10^d * (f-1)/2^(g+untenshift) * unten
    // bis 10^d * (f+1)/2^g * oben
    // einschachteln. Hierin sind die Zahlen der Form 10^d * a
    // die mit a1' <= a <= a2', wobei a1' <= a1 <= a2 <= a2' ist
    // und sich a1' und a2' analog zu a1 und a2 berechnen.
    // Da (f-1)*oben/2^g und (f+1)*oben/2^g sich um 2*oben/2^g
    // < 2^(l+2-g) < 1 unterscheiden, unterscheiden sich a2 und
    // a2' um h�chstens 1.
    // Ebenso, wenn 'oben' durch 'unten/2^untenshift' ersetzt
    // wird: a1' und a1 unterscheiden sich um h�chstens 1.
    // Ist nun a1' < a1 oder a2 < a2' , so ist die Zweierpotenz-
    // N�herung 10^d * f/2^g f�r 2^e nicht genau genug gewesen,
    // und man hat das Ganze mit erh�htem h zu wiederholen.
    // Ausnahme (da hilft auch keine h�here Genauigkeit):
    //   Wenn die obere oder untere Intervallgrenze (x+x2)/2 bzw.
    //   (x+x1)/2 selbst die Gestalt 10^d * a mit ganzem a hat.
    //   Dies testet man so:
    //     (x+x2)/2 = 2^e * oben == 10^d * a  mit ganzem a, wenn
    //     - f�r e>=0, (dann 0 <= d <= e): 5^d | oben,
    //     - f�r e<0, (dann e <= d < 0): 2^(d-e) | oben, was
    //                nur f�r d-e=0 der Fall ist.
    //     (x+x1)/2 = 2^(e-untenshift) * unten == 10^d * a
    //     mit ganzem a, wenn
    //     - f�r e>0, (dann 0 <= d < e): 5^d | unten,
    //     - f�r e<=0, (dann e <= d <= 0): 2^(d-e+untenshift) | unten,
    //                 was nur f�r d-e+untenshift=0 der Fall ist.
    // Da wir es jedoch mit gro�em |e| zu tun haben, kann dieser
    // Ausnahmefall hier gar nicht eintreten!
    // Denn im Falle e>=0: Aus e>=2*l und l>=11 folgt
    //   e >= (l+2)*ln(10)/ln(5) + ln(10)/ln(2),
    //   d >= e*ln(2)/ln(10)-1 >= (l+2)*ln(2)/ln(5),
    //   5^d >= 2^(l+2),
    //   und wegen 0 < unten < 2^(l+2) und 0 < oben < 2^(l+1)
    //   sind unten und oben nicht durch 5^d teilbar.
    // Und im Falle e<=0: Aus -e>=2*l und l>=6 folgt
    //   -e >= (l+2)*ln(10)/ln(5),
    //   d-e >= e*ln(2)/ln(10)-1-e = (1-ln(2)/ln(10))*(-e)-1
    //          = (-e)*ln(5)/ln(10)-1 >= l+1,
    //   2^(d-e) >= 2^(l+1),
    //   und wegen 0 < unten < 2^(l+1+untenshift) ist unten nicht
    //   durch 2^(d-e+untenshift) teilbar, und wegen
    //   0 < oben < 2^(l+1) ist oben nicht durch 2^(d-e) teilbar.
    {
      var cl_I a1prime = plus1(ash(minus1((f-1)*unten),-(g+untenshift)));
      if (a1prime < a1)
        { h = 2*h; goto neue_schutzbits; } // h verdoppeln und alles wiederholen
      var cl_I a2prime = ash((f+1)*oben,-g);
      if (a2 < a2prime)
        { h = 2*h; goto neue_schutzbits; } // h verdoppeln und alles wiederholen
    }
    // Jetzt ist a1 der kleinste und a2 der gr��te Wert, der
    // f�r a m�glich ist.
    // Wegen  oben - unten/2^untenshift <= 2
    // ist die obige Intervall-L�nge
    // = 10^d * ((f-1)*oben - (f+1)*unten/2^untenshift) / 2^g
    // < 10^d * ((f-1)*oben - (f-1)*unten/2^untenshift) / 2^g
    // = 10^d * (f-1)/2^g * (oben - unten/2^untenshift)
    // < 10^d * 10 * 2,
    // also gibt es h�chstens 20 m�gliche Werte f�r a.
  } else {
    // |e| ist recht klein -> man kann 2^e und 10^d exakt ausrechnen
    if (!minusp(e)) {
      // e >= 0. Sch�tze d = floor(e*lg(2)) wie oben.
      // Es ist e<=2*l<2^33.
      d = (e <= 22000
           ? floor1(e*28,93) // N�herungsbruch 28/93
           : floor1(e*76573,254370) // N�herungsbruch 76573/254370
          );
      // Das wahre d wird durch diese Sch�tzung entweder getroffen
      // oder um 1 �bersch�tzt, aber das k�nnen wir leicht feststellen.
      zehn_d = The(cl_I)(expt(10,d)); // zehn_d = 10^d
      if (ash(1,e) < zehn_d) // falls 2^e < 10^d,
        { d = d-1; zehn_d = exquo(zehn_d,10); } // Sch�tzung korrigieren
      // Nun ist 10^d <= 2^e < 10^(d+1) und zehn_d = 10^d.
      // a1 sei das kleinste ganze a > 2^(e-untenshift) * unten / 10^d,
      // a2 sei das gr��te ganze a < 2^e * oben / 10^d.
      // a1 = 1+floor(unten*2^e/(2^untenshift*10^d)),
      // a2 = floor((oben*2^e-1)/10^d).
      a1 = plus1(floor1(ash(unten,e),ash(zehn_d,untenshift)));
      a2 = floor1(minus1(ash(oben,e)),zehn_d);
    } else {
      // e < 0. Sch�tze d = floor(e*lg(2)) wie oben.
      // Es ist |e|<=2*l<2^33.
      d = (e >= -970
           ? floor1(e*3,10) // N�herungsbruch 3/10
           : floor1(e*21306,70777) // N�herungsbruch 21306/70777
          );
      // Das wahre d wird durch diese Sch�tzung entweder getroffen
      // oder um 1 �bersch�tzt, aber das k�nnen wir leicht feststellen.
      zehn_d = The(cl_I)(expt(10,-d)); // zehn_d = 10^(-d)
      if (integer_length(zehn_d) <= -e) // falls 2^e < 10^d,
        { d = d-1; zehn_d = zehn_d*10; } // Sch�tzung korrigieren
      // Nun ist 10^d <= 2^e < 10^(d+1) und zehn_d = 10^(-d).
      // a1 sei das kleinste ganze a > 2^(e-untenshift) * unten / 10^d,
      // a2 sei das gr��te ganze a < 2^e * oben / 10^d.
      // a1 = 1+floor(unten*10^(-d)/2^(-e+untenshift)),
      // a2 = floor((oben*10^(-d)-1)/2^(-e))
      a1 = plus1(ash(unten*zehn_d,e-untenshift));
      a2 = ash(minus1(oben*zehn_d),e);
    }
  }
  // Nun sind die ganzen a mit (x+x1)/2 < 10^d * a < (x+x2)/2 genau
  // die ganzen a mit a1 <= a <= a2. Deren gibt es h�chstens 20.
  // Diese werden in drei Schritten auf einen einzigen reduziert:
  // 1. Enth�lt der Bereich eine durch 10 teilbare Zahl a ?
  //    ja -> setze a1:=ceiling(a1/10), a2:=floor(a2/10), d:=d+1.
  // Danach enth�lt der Bereich a1 <= a <= a2 h�chstens 10
  // m�gliche Werte f�r a.
  // 2. Falls jetzt einer der m�glichen Werte durch 10 teilbar ist
  //    (es kann nur noch einen solchen geben),
  //    wird er gew�hlt, die anderen vergessen.
  // 3. Sonst wird unter allen noch m�glichen Werten der zu x
  //    n�chstgelegene gew�hlt.
  var cl_boolean d_shift = cl_false; // Flag, ob im 1. Schritt d incrementiert wurde
  var cl_I a; // das ausgew�hlte a
  // 1.
  {
    var cl_I b1 = ceiling1(a1,10);
    var cl_I b2 = floor1(a2,10);
    if (b1 <= b2) // noch eine durch 10 teilbare Zahl a ?
      { a1 = b1; a2 = b2; d = d+1; d_shift = cl_true; }
      else
      goto keine_10_mehr;
  }
  // 2.
  a = floor1(a2,10);
  if (10*a >= a1) {
    // Noch eine durch 10 teilbare Zahl -> durch 10 teilen.
    d = d+1; // noch d erh�hen, zehn-d wird nicht mehr gebraucht
    // Nun a in einen Dezimalstring umwandeln
    // und dann Nullen am Schlu� streichen:
    var char* as = cl_decimal_string(a); // Ziffernfolge zu a>0
    var uintL las = ::strlen(as); // L�nge der Ziffernfolge
    var uintL k = las; // L�nge ohne die gestrichenen Nullen am Schlu�
    var cl_I ee = k+d; // a * 10^d = a * 10^(-k+ee)
    while (as[k-1] == '0') // eine 0 am Schlu�?
      { // ja -> a := a / 10 (wird aber nicht mehr gebraucht),
        // d := d+1 (wird aber nicht mehr gebraucht),
        k = k-1; as[k] = '\0';
      }
    return cl_decimal_decoded_float(as,k,ee,sign);
  }
  // 3.
  keine_10_mehr:
  if (a1 == a2) {
    // a1=a2 -> keine Frage der Auswahl mehr:
    a = a1;
  } else {
    // a1<a2 -> zu x n�chstgelegenes 10^d * a w�hlen:
    if (e_gross) {
      // a = round(f*2*binmant/2^g/(1oder10)) (beliebige Rundung)
      //   = ceiling(floor(f*2*binmant/(1oder10)/2^(g-1))/2) w�hlen:
      var cl_I temp = f * binmant2;
      if (d_shift) { temp = floor1(temp,10); }
      a = ash(plus1(ash(temp,1-g)),-1);
    } else {
      // |e| klein -> analog wie oben a2 berechnet wurde
      if (!minusp(e)) {
        // e>=0: a = round(2^e*2*binmant/10^d)
        if (d_shift) { zehn_d = 10*zehn_d; }
        a = round1(ash(binmant2,e),zehn_d);
      } else {
        // e<0, also war d<0, jetzt (wegen Schritt 1) d<=0.
        // a = round(2*binmant*10^(-d)/2^(-e))
        if (d_shift) { zehn_d = floor1(zehn_d,10); }
        a = ash(plus1(ash(binmant2*zehn_d,e+1)),-1);
      }
    }
  }
  var char* as = cl_decimal_string(a); // Ziffernfolge zu a>0
  var uintL k = ::strlen(as);
  ASSERT(as[k-1] != '0');
  return cl_decimal_decoded_float(as,k,k+d,sign);
}

// Ausgabefunktion:
void print_float (std::ostream& stream, const cl_print_float_flags& flags, const cl_F& z)
{
  var cl_decimal_decoded_float z_decoded = decode_float_decimal(z);
  var char * & mantstring = z_decoded.a;
  var uintL& mantlen = z_decoded.k;
  var cl_I& expo = z_decoded.e;
  var cl_I& sign = z_decoded.s;
  // arg in Dezimaldarstellung: +/- 0.mant * 10^expo, wobei
  //  mant die Mantisse: als Simple-String mantstring mit L�nge mantlen,
  //  expo der Dezimal-Exponent,
  //  sign das Vorzeichen (-1 oder 0 oder 1).
  if (eq(sign,-1)) // z < 0 ?
    fprintchar(stream,'-');
  var bool flag = (expo >= -2) && (expo <= 7); // z=0 oder 10^-3 <= |z| < 10^7 ?
  // Was ist auszugeben? Fallunterscheidung:
  // flag gesetzt -> "fixed-point notation":
  //   expo <= 0 -> Null, Punkt, -expo Nullen, alle Ziffern
  //   0 < expo < mantlen ->
  //     die ersten expo Ziffern, Punkt, die restlichen Ziffern
  //   expo >= mantlen -> alle Ziffern, expo-mantlen Nullen, Punkt, Null
  //   Nach M�glichkeit kein Exponent// wenn n�tig, Exponent 0.
  // flag gel�scht -> "scientific notation":
  //   erste Ziffer, Punkt, die restlichen Ziffern, bei mantlen=1 eine Null
  //   Exponent.
  if (flag && !plusp(expo)) {
    // "fixed-point notation" mit expo <= 0
    // erst Null und Punkt, dann -expo Nullen, dann alle Ziffern
    fprintchar(stream,'0');
    fprintchar(stream,'.');
    for (uintL i = -FN_to_L(expo); i > 0; i--)
      fprintchar(stream,'0');
    fprint(stream,mantstring);
    expo = 0; // auszugebender Exponent ist 0
  } else {
    // "fixed-point notation" mit expo > 0 oder "scientific notation"
    var uintL scale = (flag ? FN_to_L(expo) : 1);
    // Der Dezimalpunkt wird um scale Stellen nach rechts geschoben,
    // d.h. es gibt scale Vorkommastellen. scale > 0.
    if (scale < mantlen) {
      // erst scale Ziffern, dann Punkt, dann restliche Ziffern:
      { for (uintL i = 0; i < scale; i++)
          fprintchar(stream,mantstring[i]);
      }
      fprintchar(stream,'.');
      { for (uintL i = scale; i < mantlen; i++)
          fprintchar(stream,mantstring[i]);
      }
    } else {
      // scale>=mantlen -> es bleibt nichts f�r die Nachkommastellen.
      // alle Ziffern, dann scale-mantlen Nullen, dann Punkt und Null
      fprint(stream,mantstring);
      for (uintL i = mantlen; i < scale; i++)
        fprintchar(stream,'0');
      fprintchar(stream,'.');
      fprintchar(stream,'0');
    }
    expo = expo - scale; // der auszugebende Exponent ist um scale kleiner.
  }
  // Nun geht's zum Exponenten:
  var char exp_marker;
  floattypecase(z
  ,	exp_marker = 's';
  ,	exp_marker = 'f';
  ,	exp_marker = 'd';
  ,	exp_marker = 'L';
  );
  if (!flags.float_readably) {
    floatformatcase(flags.default_float_format
    ,	if (exp_marker=='s') { exp_marker = 'E'; }
    ,	if (exp_marker=='f') { exp_marker = 'E'; }
    ,	if (exp_marker=='d') { exp_marker = 'E'; }
    ,	if ((exp_marker=='L') && (len == TheLfloat(z)->len)) { exp_marker = 'E'; }
    );
  }
  if (!(flag && (exp_marker=='E'))) { // evtl. Exponent ganz weglassen
    fprintchar(stream,exp_marker);
    print_integer(stream,10,expo);
  }
  // Fertig. Aufr�umen.
  free_hook(mantstring);
}

}  // namespace cln
