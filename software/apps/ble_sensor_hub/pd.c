#include "pd.h"

#define DEFAULT_LAG 32
#define DEFAULT_THRESHOLD 2.0
#define DEFAULT_INFLUENCE 0.5
#define DEFAULT_EPSILON 0.01

static int index = 0;
static int lag = DEFAULT_LAG;
static float threshold = DEFAULT_THRESHOLD;
static int peak = 0;
static float influence = DEFAULT_INFLUENCE;
static float EPSILON = DEFAULT_EPSILON;
static float *data, *avg, *std;

void begin(int l, float th, float inf) {
  lag = l;
  threshold = th;
  influence = inf;
  data = (float *)malloc(sizeof(float) * (lag + 1));
  avg = (float *)malloc(sizeof(float) * (lag + 1));
  std = (float *)malloc(sizeof(float) * (lag + 1));
  for (int i = 0; i < lag; ++i) {
    data[i] = 0.0;
    avg[i] = 0.0;
    std[i] = 0.0;
  }
}

void chgTh(float th) {
    threshold = th;
}

float getAvg(int start, int len) {
  float x = 0.0;
  for (int i = 0; i < len; ++i)
    x += data[(start + i) % lag];
  return x / len;
}

float getPoint(int start, int len) {
  float xi = 0.0;
  for (int i = 0; i < len; ++i)
    xi += data[(start + i) % lag] * data[(start + i) % lag];
  return xi / len;
}

float getStd(int start, int len) {
  float x1 = getAvg(start, len);
  float x2 = getPoint(start, len);
  float powx1 = x1 * x1;
  float std_val = x2 - powx1;
  if (std_val > -EPSILON && std_val < EPSILON)
    return 0.0;
  else {
    return sqrt(x2 - powx1);
  }
}

void add(float newSample) {
  peak = 0;
  int i = index % lag; //current index
  int j = (index + 1) % lag; //next index
  float deviation = newSample - avg[i];
  if (deviation > threshold * std[i]) {
    data[j] = influence * newSample + (1.0 - influence) * data[i];
    peak = 1;
  }
  else if (deviation < -threshold * std[i]) {
    data[j] = influence * newSample + (1.0 - influence) * data[i];
    peak = -1;
  }
  else
    data[j] = newSample;
  avg[j] = getAvg(j, lag);
  std[j] = getStd(j, lag);
  index++;
  if (index >= 16383) //2^14
    index = lag + j;
}

float getFilt() {
  int i = index % lag;
  return avg[i];
}

float getPeak() {
  return peak;
}
