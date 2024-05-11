#include <sessionreader.h>

#include <windows.h>
#include <b64/cdecode.h>
#include <glad/glad.h>
#include <sqlite3.h>
#include <image.h>
#include <shapes.h>
#include <text.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum Properties { COLOR,POSITION,SIZE_,LAYER,RADIUS,OUTLINE,OUTLINE_THICKNESS,OUTLINE_WIDTH,VISIBLE,SUBJECT_VIEW,NUM_TOKENS,TOKENSHAPE,TOKENSIZE,TOKENCOLORS,TOKENSIZES,TOKENXS,TOKENYS,IMAGEFILE,N_PROPERTIES };
enum GraphicTypes { CIRCLE_,ELLIPSE_,TRIANGLE_,BOX_,DIAMOND_,TOKENFACTORY_,IMAGES_,FRACTAL_,N_GRAPHICTYPES };
static char propertynames[N_PROPERTIES][32];

struct Run {
	char* name;
	float time;
};

struct Element {
	int type,id,run;
	int nrows[N_PROPERTIES],curr[N_PROPERTIES];
	char** properties[N_PROPERTIES];
	float* timestamps[N_PROPERTIES];
};

struct Image {
	char* name;
	int texture;
};

struct Index {
	int index,value;
};

struct EyeData {
	float x,y,t;
};

void update_elementtime(struct Element*,int,double,int);
void extract_values(char*,float*);
unsigned char* retrieve_xml(char*);
unsigned char** sql_query(char*,char*,int,int*);
struct EyeData* load_eyedata(char*,int*);

static struct Run* runs;
struct Element* elements;
static struct Image* images;
static struct Index* layer_order;
static struct EyeData* eye_data;
static int n_runs,n_elements,n_images,eye_count;

int init_session(char* filename, float* f) {
	sprintf(propertynames[COLOR],"Color");
	sprintf(propertynames[POSITION],"Position");
	sprintf(propertynames[SIZE_],"Size");
	sprintf(propertynames[LAYER],"Layer");
	sprintf(propertynames[RADIUS],"Radius");
	sprintf(propertynames[OUTLINE],"Outline");
	sprintf(propertynames[OUTLINE_THICKNESS],"OutlineThickness");
	sprintf(propertynames[OUTLINE_WIDTH],"OutlineWidth");
	sprintf(propertynames[VISIBLE],"Visible");
	sprintf(propertynames[SUBJECT_VIEW],"SubjectView");
	sprintf(propertynames[NUM_TOKENS],"NumTokens");
	sprintf(propertynames[TOKENSHAPE],"TokenShape");
	sprintf(propertynames[TOKENSIZE],"TokenSize");
	sprintf(propertynames[TOKENCOLORS],"TokenColors");
	sprintf(propertynames[TOKENSIZES],"TokenSizes");
	sprintf(propertynames[TOKENXS],"TokenXs");
	sprintf(propertynames[TOKENYS],"TokenYs");
	sprintf(propertynames[IMAGEFILE],"ImageFile");
	
	runs = NULL;
	n_runs = 0;
	//Get Run Names + Timings
	{
		char** ret;
		int nrows;
		
		ret = (char**)sql_query(filename,"SELECT r.name,f.time FROM runs AS r, frames AS f WHERE r.firstframe == f.dataid",2,&nrows);
		if(ret == NULL) {
			fprintf(stderr,"No runs in database.\n");
			return 0;
		}
		
		for(int i = 0; i < nrows; i++) {
			char *ind1,*ind2; 
		
			ind1 = strstr(ret[i],"_");
			ind2 = strstr(ret[i],",");
			if(!ind1 || !ind2)
				break;
			
			ind1[0] = '\0';
			
			runs = realloc(runs,sizeof(struct Run)*(n_runs+1));
			runs[n_runs].name = malloc(strlen(ret[i])+1);
			memcpy(runs[n_runs].name,ret[i],strlen(ret[i])+1);
			runs[n_runs].time = atof(ind2+1);
			n_runs++;
			
			free(ret[i]);
		}
		
		free(ret);
	}
	
	for(int i = 0; i < n_runs; i++) {
		printf("%s: %f\n",runs[i].name,runs[i].time);
	}
	
	//Get the design XML
	unsigned char* xml = retrieve_xml(filename);
	char* index;

	if(xml == NULL)
		return 0;
	
	//Load visual elements
	elements = NULL;
	n_elements = 0;
	
	printf("Loading visual elements...\n");
	index = (char*)xml;
	while(1)
	{	
		index = strstr(index,"<VisualElement type=");
		if(!index)
			break;
		
		int type, bytes = strstr(index,">")-index;
		char buffer[bytes+1];
		memcpy(buffer,index,bytes);
		buffer[bytes] = '\0';
		
		/*if(strstr(buffer,"Triangle Graphic"))
		{
			type = 4;
		}*/
		if(strstr(buffer,"\"Circle Graphic\""))
		{
			type = CIRCLE_;
		}
		else if(strstr(buffer,"\"Ellipse Graphic\""))
		{
			type = ELLIPSE_;
		}
		else if(strstr(buffer,"\"Box Graphic\""))
		{
			type = BOX_;
		}
		else if(strstr(buffer,"\"Token Factory Graphic\""))
		{
			type = TOKENFACTORY_;
		}
		else if(strstr(buffer,"\"ImageGraphic\""))
		{
			type = IMAGES_;
		}
		else
		{
			index++;
			continue;
		}
		
		index = strstr(index,"id=");
		if(!index)
			break;
		char* q1 = strstr(index,"\"")+1; 
		if(!q1)
			break;
		char* q2 = strstr(q1,"\"");
		if(!q2)
			break;
		
		memcpy(buffer,q1,q2-q1);
		buffer[q2-q1] = '\0';
		
		elements = realloc(elements,sizeof(struct Element)*(n_elements+1));
		elements[n_elements].type = type;
		elements[n_elements].id = atoi(buffer);
		elements[n_elements].run = 0;
		
		for(int i = 0; i < N_PROPERTIES; i++) {
			elements[n_elements].properties[i] = NULL;
			elements[n_elements].timestamps[i] = NULL;
		}
		memset(elements[n_elements].nrows,0,sizeof(elements[n_elements].nrows));
		memset(elements[n_elements].curr,0,sizeof(elements[n_elements].curr));
		
		{
			char query[1024];
			char** ret;
			int nrows;
			
			sprintf(query,"SELECT path FROM elementlookup WHERE assetid == %d",elements[n_elements].id);
			ret = (char**)sql_query(filename,query,1,&nrows);
			
			if(ret != NULL) {
				char* ind = strstr(ret[0],":");
				if(ind) {
					ind[0] = '\0';
					for(int i = 0; i < n_runs; i++) {
						if(strcmp(ret[0],runs[i].name) == 0) {
							elements[n_elements].run |= (1 << i);
						}
					}
				}
				
				free(ret[0]);
				free(ret);
			}
		}
		
		n_elements++;
		index++;
	}
	
	//Allocate layer information
	layer_order = (struct Index*)malloc(sizeof(struct Index)*n_elements);
	memset(layer_order,0,sizeof(struct Index)*n_elements);
	
	
	//Load any images
	images = NULL;
	n_images = 0;
	
	printf("Loading images...\n");
	index = (char*)xml;
	while(1) {
		index = strstr(index,"<Parameter type=\"ImageFile\"");
		if(!index)
			break;
		
		int id,bytes = strstr(index,">")-index;
		char buffer[bytes+1];
		memcpy(buffer,index,bytes);
		buffer[bytes] = '\0';
		
		index = strstr(index,"id=");
		if(!index)
			break;
		char* q1 = strstr(index,"\"")+1; 
		if(!q1)
			break;
		char* q2 = strstr(q1,"\"");
		if(!q2)
			break;
		
		memcpy(buffer,q1,q2-q1);
		buffer[q2-q1] = '\0';
		
		id = atoi(buffer);
		
		{
			char query[2048];
			char** ret;
			int nrows;
			sprintf(query,"SELECT value FROM properties WHERE assetid == (SELECT assetid FROM propertylookup WHERE name == 'Data' AND parent == %d) LIMIT 1", id);
			ret = (char**)sql_query(filename,query,1,&nrows); 
			
			if(ret == NULL)
				break;
			
			bytes = strlen(ret[0]);
			unsigned char decode_out[bytes];
			
			base64_decodestate s;
			base64_init_decodestate(&s);
			base64_decode_block(ret[0], bytes, decode_out, &s);
			
			free(ret[0]);
			free(ret);
			
			sprintf(query,"SELECT value FROM properties WHERE assetid == (SELECT assetid FROM propertylookup WHERE name == 'Name' AND parent == %d) LIMIT 1", id);
			ret = (char**)sql_query(filename,query,1,&nrows);

			if(ret == NULL)
				break;
			
			images = realloc(images,sizeof(struct Image)*(n_images+1));
			images[n_images].name = malloc(strlen(ret[0])+1);
			memcpy(images[n_images].name,ret[0],strlen(ret[0])+1);
			images[n_images].texture = load_image(decode_out,bytes);
			if(!images[n_images].texture) {
				printf("Failed to load image.\n");
			}

			n_images++;
			
			free(ret[0]);
			free(ret);
		}
	}
	
	printf("Retrieving frame data...\n");
	//Retrieve all frame states for visual elements
	int count = 0;
	while(count < n_elements) {
		char query[2048];
		char** ret;
		int nrows;

		for(int i = 0; i < N_PROPERTIES; i++) {
			sprintf(query,"SELECT p.value,f.time FROM properties AS p, frames AS f WHERE p.assetid == (SELECT assetid FROM propertylookup WHERE parent == %d AND name == '%s') AND f.dataid == p.frameid",elements[count].id,propertynames[i]);
			ret = (char**)sql_query(filename,query,2,&nrows);
			elements[count].properties[i] = ret;
			if(ret != NULL) {
				int ccount = 1;
				for(int j = 0; j < strlen(ret[0]); j++)
					if(ret[0][j] == ',')
						ccount++;
					
				//This shouldn't happen
				if(ccount == 1)
					continue;
				
				elements[count].nrows[i] = nrows;
				elements[count].timestamps[i] = malloc(sizeof(float)*nrows);
				
				for(int j = 0; j < nrows; j++) {
					char* ind = strrchr(ret[j],',');
					elements[count].timestamps[i][j] = atof(ind+1);
					ind[1] = '\0';
				}
			}
		}
		count++;
	}
	
	printf("Loading eye data...\n");
	eye_data = load_eyedata(filename,&eye_count);
	if(!eye_data) {
		printf("No eye data.\n");
	} else {
		printf("Done processing.\n");
	}
	
	free(xml);
	
	if(f)
		*f = runs[n_runs-1].time*1000;
	
	return 1;
}

void draw_session(double elapsed, float vv, float x, float y, float z, float w) {
	int curr_run = 0, curr_eye = 0;
	vv = 1/vv;
	for(int i = 0; i < n_runs; i++) {
		if(elapsed >= runs[i].time)
			curr_run = i;
	}
	
	while(curr_eye < eye_count && eye_data[curr_eye].t < elapsed)
		curr_eye++;
	
	update_elementtime(elements,n_elements,elapsed/1000,0);
	
	for(int i = 0; i < n_elements; i++) {
		float lyr;
		extract_values(elements[i].properties[LAYER][elements[i].curr[LAYER]],&lyr);
		layer_order[i].index = i;
		layer_order[i].value = (int)round(lyr);
	}
	
	for(int i = 0; i < n_elements; i++) {
		for(int j = i+1; j < n_elements; j++) {
			if(layer_order[j].value < layer_order[i].value) {
				struct Index temp = layer_order[j];
				layer_order[j] = layer_order[i];
				layer_order[i] = temp;
			}
		}
	}
	
	glUseProgram(shapes_->shader);
	glUniform4f(shapes_->ts_loc,x,y,z,w);
	
	for(int i = 0; i < n_elements; i++) {
		struct Element* el = elements+layer_order[i].index;
		float visible = 0, subview = 0;
		
		extract_values(el->properties[VISIBLE][el->curr[VISIBLE]],&visible);
		extract_values(el->properties[SUBJECT_VIEW][el->curr[SUBJECT_VIEW]],&subview);
		if(visible != 0 && (el->run >> curr_run))
		{
			float pos[2],col[4],scale[2],outline = 0,outline_thickness = 0;
			
			memset(pos,0,sizeof(pos));
			memset(col,0,sizeof(col));
			memset(scale,0,sizeof(scale));
			
			if(el->type == IMAGES_) {
				extract_values(el->properties[POSITION][el->curr[POSITION]],pos);
				extract_values(el->properties[SIZE_][el->curr[SIZE_]],scale);
			} else {
				extract_values(el->properties[POSITION][el->curr[POSITION]],pos);
				extract_values(el->properties[COLOR][el->curr[COLOR]],col);
				extract_values(el->properties[OUTLINE][el->curr[OUTLINE]],&outline);
				if(el->type != TOKENFACTORY_)
				{
					if(el->type == CIRCLE_)
						extract_values(el->properties[RADIUS][el->curr[RADIUS]],scale);
					else
						extract_values(el->properties[SIZE_][el->curr[SIZE_]],scale);
					
					extract_values(el->properties[OUTLINE_THICKNESS][el->curr[OUTLINE_THICKNESS]],&outline_thickness);
				}
				else
				{
					extract_values(el->properties[TOKENSIZE][el->curr[TOKENSIZE]],scale);
					extract_values(el->properties[OUTLINE_WIDTH][el->curr[OUTLINE_WIDTH]],&outline_thickness);
				}
			}
			
			pos[0] = (pos[0]/800.f*2.f-1.f)*vv;
			pos[1] = -(pos[1]/600.f*2.f-1.f);
			
			col[0] /= 255;
			col[1] /= 255;
			col[2] /= 255;
			col[3] /= 255;
			
			scale[0] /= 800.f;
			scale[1] /= 600.f;
			
			outline_thickness /= 600.f;
			
			glUniform4fv(shapes_->colu_loc,1,col);
			
			switch(el->type) {
				case CIRCLE_:
					if(outline != 0) {
						draw_outlinecircle(pos[0],pos[1],scale[0]*2/vv,scale[0]*2/vv,(outline_thickness+scale[0])/scale[0],0);
					} else {
						draw_circle(pos[0],pos[1],scale[0]*2/vv,0);
					}
				break;
				case ELLIPSE_:
					if(outline != 0){
						draw_outlinecircle(pos[0],pos[1],scale[0]/vv,scale[1],(outline_thickness+scale[1])/scale[1],0);
					} else {
						draw_ellipse(pos[0],pos[1],scale[0]/vv,scale[1],0);
					}
				break;
				case BOX_:
					if(outline != 0)
					{
						draw_rectangle(pos[0],pos[1],(scale[0]+outline_thickness)/vv,(scale[1]+outline_thickness),1);
						glUniform4f(shapes_->colu_loc,0.f,0.f,0.f,1.f);	
					}
					draw_rectangle(pos[0],pos[1],scale[0],scale[1],0);
				break;
				case IMAGES_:
					float imdex;
					extract_values(el->properties[IMAGEFILE][el->curr[IMAGEFILE]],&imdex);
					if(imdex == -1)
						break;
					
					glUseProgram(image_->shader);
					glUniform4f(image_->ts_loc,x,y,z,w);
					draw_image(images[(int)imdex].texture,pos[0],pos[1],scale[0],scale[1]);
					glUseProgram(shapes_->shader);
				break;
				case TOKENFACTORY_:
					float n_toks,t_type;
					int n_tok;
					extract_values(el->properties[NUM_TOKENS][el->curr[NUM_TOKENS]],&n_toks);
					extract_values(el->properties[TOKENSHAPE][el->curr[TOKENSHAPE]],&t_type);

					n_tok = (int)round(n_toks);
					
					if(n_tok <= 0)
						break;
					
					float cols[n_tok],xs[n_tok],ys[n_tok];
					extract_values(el->properties[TOKENCOLORS][el->curr[TOKENCOLORS]],cols);
					extract_values(el->properties[TOKENXS][el->curr[TOKENXS]],xs);
					extract_values(el->properties[TOKENYS][el->curr[TOKENYS]],ys);
					
					
					for(int i = 0; i < n_tok; i++)
					{
						float r,g,b,a;
						if(cols[i] == -1)
						{
							glUniform4fv(shapes_->colu_loc,1,col);
						}
						else
						{
							int32_t c = (int32_t)cols[i];
							b = (c & 255)/255.f;
							g = ((c >> 8) & 255)/255.f;
							r = ((c >> 16) & 255)/255.f;
							a = ((c >> 24) & 255)/255.f;
							
							glUniform4f(shapes_->colu_loc,r,g,b,a);
						}

						if(xs[i] == -1)
							xs[i] = pos[0];
						else
							xs[i] = (xs[i]/800.f*2.f-1.f)*vv;
						if(ys[i] == -1)
							ys[i] = pos[1];
						else
							ys[i] = -(ys[i]/600.f*2.f-1.f);

						if(t_type == ELLIPSE_)
						{
							if(outline != 0) {
								draw_outlinecircle(xs[i],ys[i],scale[0]/vv,scale[1],(outline_thickness+scale[1])/scale[1],0);
							} else {
								draw_ellipse(xs[i],ys[i],scale[0]/vv,scale[1],0);
							}
						}
						else if(t_type == BOX_)
						{
							if(outline != 0) {
								draw_rectangle(xs[i],ys[i],(scale[0]+outline_thickness),(scale[1]+outline_thickness),1);
								glUniform4f(shapes_->colu_loc,0.f,0.f,0.f,1.f);
							}
							draw_rectangle(xs[i],ys[i],scale[0]*vv,scale[1],0);
						}
					}
				break;
			}
		}
	}
	
	//Draw eye position
	glUseProgram(text_->shader);
	glUniform4f(text_->ts_loc,x,y,z,w);
	if(curr_eye < eye_count) {
		glUniform4f(text_->colu_loc,1.f,0.f,0.f,1.f);
		render_simpletext("+",(eye_data[curr_eye].x/800*2.f-1.f),-(eye_data[curr_eye].y/600*2.f-1.f),5.5,TXT_CENTERED);
	}
}

void free_session() {

}

void update_elementtime(struct Element* elements, int n, double time, int flags)
{
	for(int i = 0; i < n; i++)
	{
		struct Element* el = elements+i;
		for(int j = 0; j < N_PROPERTIES; j++)
		{
			if(el->properties[j] != NULL)
			{
				while((el->curr[j] < el->nrows[j]) && (el->timestamps[j][el->curr[j]] < time))
					el->curr[j]++;
				if(el->curr[j] > 0)
					el->curr[j]--;
			}
		}
	}
}

void extract_values(char* str, float* arr)
{
	char buffer[1024];
	char* tok;
	int count = 0;
	
	strcpy(buffer,str);
	tok = strtok(buffer,",");
	while(tok !=  NULL)
	{
		if(isalpha(tok[0])) {
			if(strcmp(tok,"true")==0)
				arr[count] = 1;
			else if(strcmp(tok,"false")==0)
				arr[count] = 0;
			else if(strcmp(tok,"Ellipse")==0)
				arr[count] = ELLIPSE_;
			else if(strcmp(tok,"Rectangle")==0)
				arr[count] = BOX_;
			else if(strcmp(tok,"Triangle")==0)
				arr[count] = TRIANGLE_;
			else if(strcmp(tok,"Diamond")==0)
				arr[count] = DIAMOND_;
			else {
				arr[count] = -1;
				for(int i = 0; i < n_images; i++) {
					if(strcmp(tok,images[i].name)==0) {
						arr[count] = i;
						break;
					}
				}
			}
		} else {
			switch(tok[0]) {
				case '_':
					arr[count] = -1;
				break;
				break;
				case '#':
					int32_t col = 0, alpha = 0;
					char hex[7];
					char* aindex = strstr(tok,"@");
					memcpy(hex,tok+1,6);
					hex[6] = '\0';
					col = strtol(hex,NULL,16);
					
					sscanf(aindex+1,"%d",&alpha);
					alpha <<= 24;
					
					arr[count] = (col | alpha);
				break;
				case '\0':
				break;
				default:
					arr[count] = atof(tok);
			};
		}
		count++;
		tok = strtok(NULL,",");
	}
}

unsigned char* retrieve_xml(char* db_name) {
	sqlite3* db;
	sqlite3_stmt* ppStmt;
	unsigned char* ret = NULL;
    
    int rc = sqlite3_open(db_name, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", 
                sqlite3_errmsg(db));
        return NULL;
    }
	
	rc = sqlite3_prepare_v2(db, "SELECT value FROM sessioninfo WHERE key == 'DesignXML'", -1, &ppStmt, NULL);
    if (rc != SQLITE_OK)
	{
		fprintf(stderr, "Cannot prepare statement: %s\n", 
                sqlite3_errmsg(db));
        sqlite3_close(db);
		return NULL;
	}
	
	rc = sqlite3_step(ppStmt);
	if (rc == SQLITE_ROW)
	{
		const unsigned char* col = sqlite3_column_text(ppStmt,0);
		int bytes = sqlite3_column_bytes(ppStmt,0);

		ret = malloc(bytes+1);
		memcpy(ret,col,bytes);
		ret[bytes] = '\0';
	}
	else if(rc == SQLITE_ERROR)
	{
		fprintf(stderr, "Step error: %s\n", 
                sqlite3_errmsg(db));
	}
    
	sqlite3_finalize(ppStmt);
    sqlite3_close(db);

	return ret;
}

unsigned char** sql_query(char* db_name, char* query, int ncol, int* n) {
	sqlite3* db;
	sqlite3_stmt* ppStmt;
	unsigned char** ret = NULL;
    int rc, count = 0;
	
	if(n != NULL)
		*n = 0;
	
    rc = sqlite3_open(db_name, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", 
                sqlite3_errmsg(db));
        return NULL;
    }
	
	rc = sqlite3_prepare_v2(db,query, -1, &ppStmt, NULL);
    if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", 
                sqlite3_errmsg(db));
        sqlite3_close(db);
		return NULL;
	}
	
	//Count number of rows so we can preallocate memory
	while((rc = sqlite3_step(ppStmt)) == SQLITE_ROW)
		count++;

	if(count == 0)
	{
		sqlite3_finalize(ppStmt);
		sqlite3_close(db);
		return NULL;
	}
	
	ret = malloc(sizeof(unsigned char*)*count);
	count = 0;
	
	//Prepare the statement again
	rc = sqlite3_prepare_v2(db,query, -1, &ppStmt, NULL);
    if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", 
                sqlite3_errmsg(db));
        sqlite3_close(db);
		return NULL;
	}
	
	while((rc = sqlite3_step(ppStmt)) == SQLITE_ROW) {
		int total_bytes = 0;
		for(int i = 0; i < ncol; i++)
			total_bytes += sqlite3_column_bytes(ppStmt,i);
		
		ret[count] = malloc(total_bytes+ncol);
		
		int pos = 0;
		for(int i = 0; i < ncol; i++)
		{
			const unsigned char* col = sqlite3_column_text(ppStmt,i);
			int bytes = sqlite3_column_bytes(ppStmt,i);
			
			memcpy(ret[count]+pos,col,bytes);
			pos += bytes;
			if(i != ncol-1)
			{
				ret[count][pos] = ',';
				pos++;
			}
		}
		ret[count][pos] = '\0';
		
		count++;
	}
	if(rc == SQLITE_ERROR) {
		fprintf(stderr, "Step error: %s\n", 
                sqlite3_errmsg(db));
	}
	
	if(n != NULL)
		*n = count;
    
	sqlite3_finalize(ppStmt);
    sqlite3_close(db);

	return ret;
}

struct EyeData* load_eyedata(char* db_name, int* n) {
	sqlite3* db;
	sqlite3_stmt* ppStmt;
	struct EyeData* dat = NULL;
    int rc, blobsize = 0, count = 0;
	
	if(n)
		*n = 0;
	
    rc = sqlite3_open(db_name, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", 
                sqlite3_errmsg(db));
        return NULL;
    }
	
	//Get count of eye samples
	rc = sqlite3_prepare_v2(db,"SELECT SUM(length(data)) FROM signal_position", -1, &ppStmt, NULL);
    if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", 
                sqlite3_errmsg(db));
        goto cleanup;
	}
	
	rc = sqlite3_step(ppStmt);
	if (rc == SQLITE_ROW) {
		blobsize = sqlite3_column_int(ppStmt,0);
	}
	else if(rc == SQLITE_ERROR) {
		fprintf(stderr, "Step error: %s\n", 
                sqlite3_errmsg(db));
		goto cleanup;
	}
	
	if (blobsize == 0) {
		fprintf(stderr, "No eye data.\n");
		goto cleanup;
	}
	
	//Allocate memory for eye position and time
	dat = (struct EyeData*)malloc(blobsize*3/2);
	
	//Retrieve eye data
	rc = sqlite3_prepare_v2(db,"SELECT eye.data,fr.time FROM signal_position AS eye LEFT JOIN frames AS fr ON eye.frameid = fr.dataid", -1, &ppStmt, NULL);
    if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", 
                sqlite3_errmsg(db));
		free(dat);
		dat = NULL;
        goto cleanup;
	}
	
	while((rc = sqlite3_step(ppStmt)) == SQLITE_ROW) {
		int samples = sqlite3_column_bytes(ppStmt,0)/8;
		float* data = (float*)sqlite3_column_blob(ppStmt,0);
		float t = sqlite3_column_double(ppStmt,1)*1000;
		
		for(int i = 0; i < samples; i++) {
			dat[count].x = data[i*2];
			dat[count].y = data[i*2+1];
			dat[count].t = t+i*2;
			count++;
		}
	}
	if(rc == SQLITE_ERROR) {
		fprintf(stderr, "Step error: %s\n", 
                sqlite3_errmsg(db));
		free(dat);
		dat = NULL;
		count = 0;
		goto cleanup;
	}

cleanup:
	sqlite3_finalize(ppStmt);
    sqlite3_close(db);
	
	if(n)
		*n = count;
	
	return dat;
}






































