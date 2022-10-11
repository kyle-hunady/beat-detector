/*
   This code is mostly taken from
   https://damian.pecke.tt/2015/03/02/beat-detection-on-the-arduino.html

   It originally used an external pot to set the threshold for the beat detection.
   Now it tries to automagically do it based on an average of past states compared to the current state.

   The modifications and construction were made in  major-hurry.
   Sorry for the low quality, but it's free and it's still better than nothing.
   Use at own risk.

*/

#define SAMPLING_INTERVAL 200 // default 200 us (5000Hz)

// define setting and clearing registers
#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif
#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif

void setup() {
  // set ADC to 77khz (max for 10bit)
  sbi(ADCSRA, ADPS2);
  cbi(ADCSRA, ADPS1);
  cbi(ADCSRA, ADPS0);

  // set LED pins to output mode
  pinMode(2, OUTPUT);
  pinMode(3, OUTPUT);
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
  pinMode(6, OUTPUT);
  pinMode(13, OUTPUT);
  
  Serial.begin(115200);
}

// 20 - 200Hz single pole band-pass IIR filter
float bassFilter(float sample) {
  static float xv[3] = {0, 0, 0};
  static float yv[3] = {0, 0, 0};
  
  xv[0] = xv[1]; xv[1] = xv[2];
  xv[2] = sample / 9.1f;
  yv[0] = yv[1]; yv[1] = yv[2];
  yv[2] = (xv[2] - xv[0])
        + (-0.7960060012f * yv[0]) 
        + (1.7903124146f * yv[1]);
        
  return yv[2];
}

// 10Hz single pole low-pass IIR filter
float envelopeFilter(float sample) {
  static float xv[2] = {0, 0};
  static float yv[2] = {0, 0};
  
  xv[0] = xv[1];
  xv[1] = sample / 160.f;
  yv[0] = yv[1];
  yv[1] = (xv[0] + xv[1]) + (0.9875119299f * yv[0]);
  
  return yv[1];
}

// 1.7 - 3.0Hz single pole band-pass IIR filter
float beatFilter(float sample) {
  static float xv[3] = {0, 0, 0};
  static float yv[3] = {0, 0, 0};
  
  xv[0] = xv[1];  xv[1] = xv[2]; // shift values
  xv[2] = sample / 7.015f;
  
  yv[0] = yv[1];  yv[1] = yv[2]; // shift values
  yv[2] = (xv[2] - xv[0]) 
        + (-0.7169861741f * yv[0]) 
        + (1.4453653501f * yv[1]);
  
  return yv[2];
}


// createString does 2 things:
// 1. creates a string of '=' to be set over uart for devel purposes.
//       VU-meter style. the length is proportional to the value of the final filter.
// 2. as an afterthought, 5 leds were added/controlled as a real-life vu-meter for quick debugging at the music venue.

void createString(float size, char * string) {

  if (size > 5 )
    digitalWrite(2, HIGH);
  else
    digitalWrite(2, LOW);

  if (size > 10 )
    digitalWrite(3, HIGH);
  else
    digitalWrite(3, LOW);

  if (size > 20 )
    digitalWrite(4, HIGH);
  else
    digitalWrite(4, LOW);

  if (size > 30 )
    digitalWrite(5, HIGH);
  else
    digitalWrite(5, LOW);

  if (size > 40 )
    digitalWrite(6, HIGH);
  else
    digitalWrite(6, LOW);


  // offset and integerify size
  int val = 15 + ((int)size);

  // hard limit to 0-29
  if (val < 0) val = 0;
  if (val > 29) val = 29;

  // build string
  int i = 0;
  for (i = 0; i < val ; i++) {
    *string = '=';
    string++;
  }
  *string = 0;//end with null
}


// beatJudge decides if a "valid" beat is detected
// it does the "magic" of averaging the last 9-ish samples from the
// last filter and comparing it to the current sample
// this code was all written and adjusted from head and real-life tests
// so excuse the quality of the late night ballmer's peak result

int beatJudge(float val) {
  static float history[10];
  float avg = 0;

  // compute average of last 9 samples (hopefully)
  for (int i = 9; i >= 0; i--) {
    avg += history[i];
  }
  avg = avg / 9;


  // write history (heh, see what I did there? no? nevermind. Just pushing newest value on FIFO)
  for (int i = 0; i < 8; i++) {
    history[i] = history[i + 1];
  }
  history[9] = val;

  // debugging
  //Serial.println("Avg:");
  //Serial.println(avg);
  //Serial.println(val);

  // if there's a fast rise in low freq volume
  if ((avg * 145) < (val - 45)) { // "magic" (adapt this garbage to something that works better, if possible)
    return 1;
  } else {
    // fake news
    return 0;
  }

}

void loop() {
  unsigned long time = micros(); // sample rate timer
  float sample, value, envelope, beat, thresh;
  unsigned char i;
  char buff[35];

  for (i = 0;; ++i) {
    // read ADC and center around 512 (halfway)
    sample = (float)analogRead(1) - 358.f; // 358 was experimentally obtained
    // basically, just see what approximate value you get from analogRead() when no signal present
    // you want "sample" to be as close to zero as possible when no signal (music) comes in

    // use this to determine the offset from above (358 in my case)
    // Serial.println(sample);

    // filter only bass component
    value = bassFilter(sample);

    // take signal amplitude and filter (basically get the envelope of the low freq signals)
    if (value < 0)value = -value;
    envelope = envelopeFilter(value);

    // every 200 samples (25hz) filter the envelope
    if (i == 200) {
      // filter out repeating bass sounds 100 - 180bpm
      beat = beatFilter(envelope);
      //Serial.println(beat);
      createString(beat, buff);
      //Serial.println(buff);

      // control the MAIN output led here
      // low and high were inversed because the entire filter chain is so slow (phase delay),
      // it better matches the actual time you hear the beats.
      // the downside is that it "fails" the first 1-2 beats, until it syncs
      if (beatJudge(beat)) {
        digitalWrite(13, LOW);
        //Serial.println(">>>");
      }
      else digitalWrite(13, HIGH);

      // reset sample counter
      i = 0;
    }

    // consume excess clock cycles to maintain 5000Hz sampling
    for (unsigned long up = time + SAMPLING_INTERVAL; time > 20 && time < up;
         time = micros());
  }

}
