// A 3d model
// (C)+(W) by Thorsten Jordan. See LICENSE

#ifndef MODEL_H
#define MODEL_H

#include "vector3.h"
#include "matrix4.h"
#include "texture.h"
#include "color.h"
#include <vector>
#include <fstream>

class TiXmlElement;

class model {
public:
	typedef std::auto_ptr<model> ptr;

	class material {
		material(const material& );
		material& operator= (const material& );
	public:
		class map {
			map(const map& );
			map& operator= (const map& );
		public:
			std::string filename;	// also in mytexture, fixme
			float uscal, vscal, uoffset, voffset;
			float angle;	// uv rotation angle;
			texture* mytexture;
			map() : uscal(1.0f), vscal(1.0f), uoffset(0.0f), voffset(0.0f), angle(0.0f), mytexture(0) {}
			~map() { delete mytexture; }
			void init(int mapping, bool makenormalmap = false, float detailh = 1.0f);
			void write_to_dftd_model_file(TiXmlElement* parent,
						      const std::string& type) const;
			// read and construct from dftd model file
			map(TiXmlElement* parent);
		};
	
		std::string name;
		color ambient;
		color diffuse;
		color specular;
		color transparency;//????
		map* tex1;
		map* bump;
		//map* specularmap;
		
		material() : tex1(0), bump(0) {}
		void init(void);
		~material() { delete tex1; delete bump; }
		void set_gl_values(void) const;
	};
	
	struct mesh {
		std::string name;
		std::vector<vector3f> vertices;
		std::vector<vector3f> normals;
		std::vector<vector3f> tangentsx;
		std::vector<vector2f> texcoords;
		std::vector<bool> righthanded;	// fixme, hack
		std::vector<unsigned> indices;	// 3 indices per face
		matrix4f transformation;	// rot., transl., scaling
		material* mymaterial;
		vector3f min, max;

		void display(bool usematerial) const;
		void compute_bounds(void);	
		void compute_normals(void);
		bool compute_tangentx(unsigned i0, unsigned i1, unsigned i2);

		mesh() : transformation(matrix4f::one()), mymaterial(0) {}
		~mesh() {}

		// transform vertices by matrix
		void transform(const matrix4f& m);
		void write_off_file(const std::string& fn) const;

		// give plane equation (abc must have length 1)
		pair<mesh, mesh> split(const vector3f& abc, float d) const;
	};

	struct light {
		std::string name;
		vector3f pos;
		float colr, colg, colb;
		void set_gl(unsigned nr_of_light) const;
		light() : colr(1.0f), colg(1.0f), colb(1.0f) {}
		~light() {}
	};

protected:	
	std::vector<material*> materials;
	std::vector<mesh> meshes;
	std::vector<light> lights;
	
	unsigned display_list;	// OpenGL display list for the model
	bool usematerial;

	std::string basename;	// base name of the scene/model, computed from filename

	vector3f min, max;

	// class-wide variables: shaders supported and enabled, shader number and init count
	static unsigned init_count;

	// Booleans for supported OpenGL extensions
	static bool vertex_program_supported;
	static bool fragment_program_supported;
	static bool compiled_vertex_arrays_supported;

	// Config options (only used when supported and enabled)
	static bool use_vertex_programs;
	static bool use_fragment_programs;

	// Shader programs
	static GLuint default_vertex_program;
	static GLuint default_fragment_program;

	// init / deinit
	static void render_init(void);
	static void render_deinit(void);

	void compute_bounds(void);
	void compute_normals(void);
	
	std::vector<float> cross_sections;	// array over angles
	
	void read_cs_file(const std::string& filename);
	
	// ------------ 3ds loading functions ------------------
	struct m3ds_chunk {
		unsigned short id;
		unsigned bytes_read;
		unsigned length;
		bool fully_read(void) const { return bytes_read >= length; }
		void skip(istream& in);
	};
	void m3ds_load(const std::string& fn);
	std::string m3ds_read_string(istream& in, m3ds_chunk& ch);
	m3ds_chunk m3ds_read_chunk(istream& in);
	std::string m3ds_read_string_from_rest_of_chunk(istream& in, m3ds_chunk& ch);
	void m3ds_process_toplevel_chunks(istream& in, m3ds_chunk& parent);
	void m3ds_process_model_chunks(istream& in, m3ds_chunk& parent);
	void m3ds_process_object_chunks(istream& in, m3ds_chunk& parent, const std::string& objname);
	void m3ds_process_trimesh_chunks(istream& in, m3ds_chunk& parent, const std::string& objname);
	void m3ds_process_light_chunks(istream& in, m3ds_chunk& parent, const std::string& objname);
	void m3ds_process_face_chunks(istream& in, m3ds_chunk& parent, mesh& m);
	void m3ds_process_material_chunks(istream& in, m3ds_chunk& parent);
	void m3ds_process_materialmap_chunks(istream& in, m3ds_chunk& parent, material::map* m);
	void m3ds_read_color_chunk(istream& in, m3ds_chunk& ch, color& col);
	void m3ds_read_faces(istream& in, m3ds_chunk& ch, mesh& m);
	void m3ds_read_uv_coords(istream& in, m3ds_chunk& ch, mesh& m);
	void m3ds_read_vertices(istream& in, m3ds_chunk& ch, mesh& m);
	void m3ds_read_material(istream& in, m3ds_chunk& ch, mesh& m);
	// ------------ end of 3ds loading functions ------------------
	
	model(const model& );
	model& operator= (const model& );

	void read_off_file(const std::string& fn);

	void read_dftd_model_file(const std::string& filename);
	void write_color_to_dftd_model_file(TiXmlElement* parent, const color& c,
					    const std::string& type) const;
	color read_color_from_dftd_model_file(TiXmlElement* parent, const std::string& type);

public:
	model();

	static int mapping;	// GL_* mapping constants (default GL_LINEAR_MIPMAP_LINEAR)
	static bool enable_vertex_programs;	// en-/disable use of VP (default true)
	static bool enable_fragment_programs;	// en-/disable use of FP (default true)

	model(const std::string& filename, bool usematerial = true, bool makedisplaylist = true);
	~model();
	void display(void) const;
	mesh& get_mesh(unsigned nr);
	const mesh& get_mesh(unsigned nr) const;
	material& get_material(unsigned nr);
	const material& get_material(unsigned nr) const;
	light& get_light(unsigned nr);
	const light& get_light(unsigned nr) const;
	unsigned get_nr_of_meshes(void) const { return meshes.size(); }
	unsigned get_nr_of_materials(void) const { return materials.size(); }
	unsigned get_nr_of_lights(void) const { return lights.size(); }
	vector3f get_min(void) const { return min; }
	vector3f get_max(void) const { return max; }
	float get_length(void) const { return (max - min).y; }
	float get_width(void) const { return (max - min).x; }
	float get_height(void) const { return (max - min).z; }
	vector3f get_boundbox_size(void) const { return max-min; }
	float get_cross_section(float angle) const;	// give angle in degrees.
	static std::string tolower(const std::string& s);
	void add_mesh(const mesh& m) { meshes.push_back(m); }//fixme: maybe recompute bounds
	void add_material(material* m) { materials.push_back(m); }
	// transform meshes by matrix (attention: scaling destroys normals)
	void transform(const matrix4f& m);

	// write our own model file format.
	void write_to_dftd_model_file(const std::string& filename, bool store_normals = true) const;
};	

#endif
