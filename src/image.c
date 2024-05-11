#include <image.h>
#include <video.h>
#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image/stb_image_write.h>

image_struct* image_ = NULL;

static const char *vertexShader = "#version 130\n"
	"#extension GL_ARB_explicit_attrib_location : enable\n"
	"layout (location = 0) in vec3 aPos;"
	"layout (location = 1) in vec3 aColor;"
	"layout (location = 2) in vec2 aTexCoord;"
	"out vec3 ourColor;"
	"out vec2 TexCoord;"
	"uniform vec4 pos_scale;"
	"uniform vec4 tscale;"
	"uniform vec2 viewPort;"
	"void main()"
	"{"
	"	vec4 p = vec4(aPos.x*pos_scale.z+pos_scale.x,aPos.y*pos_scale.w+pos_scale.y,0.0,1.0);"
	"	gl_Position = vec4(p.x*tscale.z+tscale.x,p.y*tscale.w+tscale.y,0.0,1.0);"
	"	ourColor = aColor;"
	"	TexCoord = aTexCoord;"
	"}\0";
	
static const char *fragmentShader = "#version 130\n"
	"out vec4 FragColor;"
	"in vec3 ourColor;"
	"in vec2 TexCoord;"
	"uniform sampler2D texture1;"
	"void main()"
	"{"
	"	FragColor = texture(texture1, TexCoord);"
	"}\0";

int init_image()
{
	image_ = (image_struct*)malloc(sizeof(image_struct));
	memset(image_,0,sizeof(image_struct));
	
	image_->shader = create_program(vertexShader, fragmentShader);
	if(!image_->shader)
		return 0;
	
	glUseProgram(image_->shader);
	
	image_->psu_loc = glGetUniformLocation(image_->shader,"pos_scale");
	image_->ts_loc = glGetUniformLocation(image_->shader,"tscale");

	glUniform2f(glGetUniformLocation(image_->shader,"viewPort"),video_->w,video_->h);
	
	float vertices[] = {
    // positions          // colors           // texture coords
     -1.f, 1.f, 0.f,   1.f, 0.f, 0.f,   0.f, 1.f,   // top left
     -1.f, -1.f, 0.f,  0.f, 1.f, 0.f,   0.f, 0.f,   // bottom left
      1.f, 1.f, 0.f,   0.f, 0.f, 1.f,   1.f, 1.f,   // top right
      1.f, -1.f, 0.f,  1.f, 1.f, 0.f,   1.f, 0.f    // bottom right
	};
	
	glGenVertexArrays(1, &image_->VAO);
    glGenBuffers(1, &image_->VBO);
	
	glBindVertexArray(image_->VAO);
	
	glBindBuffer(GL_ARRAY_BUFFER, image_->VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	
	// position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // color attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // texture coord attribute
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
	
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	
	glActiveTexture(GL_TEXTURE0);
	
	return 1;
}

unsigned int load_image(const unsigned char* buffer, int len)
{
	unsigned int tex = 0;
	int w,h,ch;
	
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D,tex);
	
	// set the texture wrapping/filtering options (on the currently bound texture object)
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	
	stbi_set_flip_vertically_on_load(1);
	
	// load and generate the texture
	unsigned char *data = stbi_load_from_memory(buffer,len,&w,&h,&ch,0);

	if(data)
	{
		if(ch == 3)
			glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,w,h,0,GL_RGB,GL_UNSIGNED_BYTE,data);
		else if(ch == 4)
			glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
		else {
			fprintf(stderr,"Invalid channel count: %d\n",ch);
			stbi_image_free(data);
			glDeleteTextures(1, &tex);
			return 0;
		}
		glGenerateMipmap(GL_TEXTURE_2D);
		
		stbi_image_free(data);
	}
	else {
		glDeleteTextures(1, &tex);
		return 0;
	}
	
	return tex;
}

void draw_image(int tex, float x, float y, float scalex, float scaley)
{
	glBindTexture(GL_TEXTURE_2D,tex);
	
	glUniform4f(image_->psu_loc,x,y,scalex,scaley);
	
	glBindVertexArray(image_->VAO);
	glDrawArrays(GL_TRIANGLE_STRIP,0,4);
}

void update_imagevp()
{
	glUseProgram(image_->shader);
	glUniform2f(glGetUniformLocation(image_->shader,"viewPort"),video_->w,video_->h);
}

void destroy_image()
{
	if(image_ != NULL)
	{
		glDeleteVertexArrays(1, &image_->VAO);
		glDeleteBuffers(1, &image_->VBO);
		glDeleteProgram(image_->shader);
		
		free(image_);
		image_ = NULL;
	}
}