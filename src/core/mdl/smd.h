#pragma once

// Source Model Data
namespace smd
{
	enum StudioModelDataVersion_t : uint32_t
	{
		SMD_INVALID = 0u,
		SMD_V1, // basic run of the mill smd
		SMD_V2, // unknown
		SMD_V3, // support for up to 8 texcoords (adjust maxTexcoords, currently have it set low because we don't use it and it hogs memory with the way vertices are setup)
	};

	static uint32_t s_outputVersion = SMD_V1;

	constexpr uint32_t maxBoneWeights = 3;
	constexpr uint32_t maxTexcoords = 8;

	class Node
	{
	public:
		Node() : name(nullptr), index(-1), parent(-1) {};

		const char* name;
		int index;
		int parent;
	};

	class Bone
	{
	public:
		Bone(const int nodeid, const Vector& position, const RadianEuler& rotation) : node(nodeid), pos(position), rot(rotation) {}

		int node;

		Vector pos;
		RadianEuler rot;
	};

	class Frame
	{
	public:
		std::vector<Bone> bones;
	};

	class Vertex
	{
	public:
		Vertex()
		{
			memset(this, 0, sizeof(Vertex));
		}

		Vertex(const Vertex* const vert) : position(vert->position), normal(vert->normal), texcoords(), numTexcoords(vert->numTexcoords), numBones(vert->numBones), weight(), bone()
		{
			memcpy_s(texcoords, sizeof(texcoords), vert->texcoords, sizeof(vert->texcoords));
			memcpy_s(weight, sizeof(weight), vert->weight, sizeof(vert->weight));
			memcpy_s(bone, sizeof(bone), vert->bone, sizeof(vert->bone));
		}

		Vector position;
		Vector normal;

		Vector2D texcoords[maxTexcoords];
		uint32_t numTexcoords;

		uint32_t numBones;
		float weight[maxBoneWeights];
		int bone[maxBoneWeights];
	};

	class Triangle
	{
	public:
		Triangle(const char* mat) : material(mat), vertices() {}

		const char* material;
		size_t vertices[3];
	};

	class CStudioModelData
	{
	public:
		CStudioModelData(const std::filesystem::path& path, const size_t nodeCount, const size_t frameCount) : exportPath(path), numNodes(nodeCount), nodes(nullptr), numFrames(frameCount), frames(nullptr), vertexIndex(0ull)
		{
			assertm(numNodes, "must have at least one bone");
			assertm(numFrames, "must have at least one frame");

			nodes = new Node[numNodes];
			frames = new Frame[numFrames];

			for (size_t i = 0; i < numFrames; i++)
			{
				Frame& frame = frames[i];
				frame.bones.reserve(numNodes);
			}
		}

		~CStudioModelData()
		{
			FreeAllocArray(nodes);
			FreeAllocArray(frames);
		}

		void InitNode(const char* name, const int index, const int parent) const;
		void InitFrameBone(const int iframe, const int ibone, const Vector& pos, const RadianEuler& rot) const;
		void UpdateFrameBone(const int iframe, const int ibone, const Vector& pos, const RadianEuler& rot) const;
		void InitVertex(const Vertex* const vert) { vertices.emplace_back(vert); }
		// indices local to mesh vertices
		void InitLocalTriangle(const char* material, const uint32_t indice0, const uint32_t indice1, const uint32_t indice2)
		{
			triangles.emplace_back(material);

			Triangle* const topTri = TopTri();

			topTri->vertices[0] = vertexIndex + indice0;
			topTri->vertices[1] = vertexIndex + indice1;
			topTri->vertices[2] = vertexIndex + indice2;
		}

		void AddMeshCapacity(const size_t numVertices, const size_t numTriangles)
		{
			//assertm(triangles.size() == triangles.capacity(), "mismatched indices");

			vertexIndex = vertices.size(); // assume we're adding a mesh, so update the index

			vertices.reserve(vertices.size() + numVertices);
			triangles.reserve(triangles.size() + numTriangles);
		}

		Triangle* const TopTri() { return &triangles.back(); }

		void SetName(const std::string& name) { exportName = name; }
		void SetPath(const std::filesystem::path& path) { exportPath = path; }

		const size_t NodeCount() const { return numNodes; }
		const size_t FrameCount() const { return numFrames; }

		// so we don't have to re parse nodes
		void ResetMeshData()
		{
			vertexIndex = 0u;

			vertices.clear();
			triangles.clear();
		}
		void ResetFrameData(const size_t frameCount)
		{
			FreeAllocArray(frames);

			numFrames = frameCount;
			frames = new Frame[numFrames];
		}

		// still slow but a lot faster than the previous implementation (~5x faster)
		const bool Write(char* const buffer, const size_t size) const;
		void Write() const;

		static void SetVersion(const StudioModelDataVersion_t version) { s_outputVersion = version; }

	private:
		size_t numNodes;
		Node* nodes;

		size_t numFrames;
		Frame* frames;

		size_t vertexIndex; // offset for newly added vertices
		std::vector<Vertex> vertices;
		std::vector<Triangle> triangles;

		std::filesystem::path exportPath;
		std::string exportName;
	};
}