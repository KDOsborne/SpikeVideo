#ifndef SHAPES_H
#define SHAPES_H

#include <glad/glad.h>

typedef struct Shapes
{
	unsigned int 	shader;
	int 			psu_loc,colu_loc,rotu_loc,upu_loc,ts_loc;
}Shapes;

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions
int		init_shapes();
void 	draw_square(float,float,float,int);
void	draw_rectangle(float,float,float,float,int);
void 	draw_rectangle_r(float,float,float,float,float,float,int);
void 	draw_circle(float,float,float,int);
void 	draw_ellipse(float,float,float,float,int);
void	draw_outlinecircle(float,float,float,float,float,int);
void 	draw_line(float,float,float,int);
void 	draw_line_r(float,float,float,float,float,int);
void    update_shapesvp();
void 	destroy_square();
void 	destroy_circle();
void 	destroy_outlinecircle();
void	destroy_line();
void 	destroy_shapes();
////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Globals
extern Shapes* shapes_;
////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif //SHAPES_H