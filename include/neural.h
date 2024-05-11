#ifndef NEURAL_H
#define NEURAL_H

int init_neural(char*,int,int,int,int,double);
int read_data(double,float*);
int read_raster(double);
void draw_spikes(double);
void draw_raster(double,float,float);
void cull_spikes(double);
void destroy_neural();

extern int TARGET_CHANNEL,liveplay;

#endif //NEURAL_H