#include <pch.h>

#include <core/mdl/smd.h>
#include <core/utils/textbuffer.h>

namespace smd
{
	void CStudioModelData::InitNode(const char* name, const int index, const int parent) const
	{
		Node& node = nodes[index];

		// [rika]: node has already been set
		if (node.name)
		{
			assertm(false, "should not be reinitalizing a node");
			return;
		}

		node.name = name;
		node.index = index;
		node.parent = parent;
	}

	void CStudioModelData::InitFrameBone(const int iframe, const int ibone, const Vector& pos, const RadianEuler& rot) const
	{
		assertm(iframe < numFrames, "frame out of range");
		assertm(ibone < numNodes, "node out of range");

		Frame* const frame = &frames[iframe];

		const int boneCount = static_cast<int>(frame->bones.size());

		if (ibone == boneCount)
		{
			frame->bones.emplace_back(ibone, pos, rot);

			return;
		}

		if (ibone < boneCount)
		{
			Bone& bone = frame->bones.at(ibone);

			bone.node = ibone;
			bone.pos = pos;
			bone.rot = rot;

			return;
		}

		assertm(false, "bones added out of order or not pre-sized");
	}

	// [rika]: this does not round floats to 6 decimal points as required by SMD, HOWEVER studiomdl supports scientific notation so we should be ok! 
	// [rika]: if this causes any weird issues in tthe future such as messed up skeletons we can change it
	void CStudioModelData::Write() const
	{
		std::filesystem::path outPath(exportPath);
		outPath.append(exportName);
		outPath.replace_extension(".smd");

		std::ofstream out(outPath, std::ios::out);

		out << "version " << s_outputVersion << "\n";

		out << "nodes\n";
		for (size_t i = 0; i < numNodes; i++)
		{
			const Node& node = nodes[i];

			out << "\t" << node.index << " \"" << node.name << "\" " << node.parent << "\n";
		}
		out << "end\n";

		out << "skeleton\n";
		for (size_t iframe = 0; iframe < numFrames; iframe++)
		{
			const Frame& frame = frames[iframe];

			out << "\ttime " << iframe << "\n";

			for (const Bone& bone : frame.bones)
			{
				out << "\t\t" << bone.node << " ";
				out << bone.pos.x << " " << bone.pos.y << " " << bone.pos.z << " ";
				out << bone.rot.x << " " << bone.rot.y << " " << bone.rot.z << "\n";
			}
		}
		out << "end\n";

		if (triangles.size())
		{
			out << "triangles\n";
			for (size_t itriangle = 0; itriangle < triangles.size(); itriangle++)
			{
				const Triangle& triangle = triangles[itriangle];

				out << "" << triangle.material << "\n";

				for (uint32_t vertIdx = 0u; vertIdx < 3u; vertIdx++)
				{
					const Vertex& vert = vertices[triangle.vertices[vertIdx]];

					out << "\t" << vert.bone[0] << " ";
					out << vert.position.x << " " << vert.position.y << " " << vert.position.z << " ";
					out << vert.normal.x << " " << vert.normal.y << " " << vert.normal.z << " ";
					out << vert.texcoords[0].x << " " << vert.texcoords[0].y << " " << vert.numBones << " ";

					for (uint32_t weightIdx = 0u; weightIdx < vert.numBones; weightIdx++)
					{
						out << vert.bone[weightIdx] << " " << vert.weight[weightIdx] << " ";
					}

					if (s_outputVersion == 3)
					{
						out << " " << vert.numTexcoords;

						for (uint32_t texcoordIdx = 0u; texcoordIdx < vert.numTexcoords; texcoordIdx++)
						{
							out << " " << vert.texcoords[texcoordIdx].x << " " << vert.texcoords[texcoordIdx].y;
						}
					}

					out << "\n";
				}
			}
			out << "end\n";
		}

		out.close();
	}

	const bool CStudioModelData::Write(char* const buffer, const size_t size) const
	{
		if (!buffer || !size)
			return false;

		CTextBuffer textBuffer(buffer, size);

		const char** const vertexLines = reinterpret_cast<const char** const>(textBuffer.Writer());
		textBuffer.AdvanceWriter(sizeof(intptr_t) * vertices.size());

		// pre parse vertex strings
		for (size_t vertIdx = 0; vertIdx < vertices.size(); vertIdx++)
		{
			vertexLines[vertIdx] = textBuffer.Writer();
			const Vertex& vert = vertices[vertIdx];

			// bone, pos xyz, normal xyz, texcoord xy, weight count 
			textBuffer.WriteFormatted("%i %f %f %f %f %f %f %f %f %i", vert.bone[0],
				vert.position.x, vert.position.y, vert.position.z,
				vert.normal.x, vert.normal.y, vert.normal.z,
				vert.texcoords[0].x, vert.texcoords[0].y, vert.numBones);

			for (uint32_t weightIdx = 0u; weightIdx < vert.numBones; weightIdx++)
			{
				textBuffer.WriteFormatted(" %i %f", vert.bone[weightIdx], vert.weight[weightIdx]);
			}

			if (s_outputVersion == 3 && vert.numTexcoords > 1)
			{
				textBuffer.WriteFormatted(" %i", vert.numTexcoords);

				for (uint32_t texcoordIdx = 0u; texcoordIdx < vert.numTexcoords; texcoordIdx++)
				{
					textBuffer.WriteFormatted(" %f %f", vert.texcoords[texcoordIdx].x, vert.texcoords[texcoordIdx].y);
				}
			}

			textBuffer.WriteCharacter('\0');
		}

		textBuffer.SetTextStart();

		textBuffer.WriteFormatted("version %i\n", s_outputVersion);
		textBuffer.WriteString("nodes\n");

		for (size_t i = 0; i < numNodes; i++)
		{
			const Node& node = nodes[i];
			textBuffer.WriteFormatted("\t%i \"%s\" %i\n", node.index, node.name, node.parent);
		}
		textBuffer.WriteString("end\n");

		textBuffer.WriteString("skeleton\n");
		for (size_t iframe = 0; iframe < numFrames; iframe++)
		{
			const Frame& frame = frames[iframe];

			textBuffer.WriteFormatted("\ttime %llu\n", iframe);

			for (const Bone& bone : frame.bones)
			{
				textBuffer.WriteFormatted("\t\t%i %f %f %f %f %f %f\n", bone.node, bone.pos.x, bone.pos.y, bone.pos.z, bone.rot.x, bone.rot.y, bone.rot.z);
			}
		}
		textBuffer.WriteString("end\n");

		if (triangles.size())
		{
			textBuffer.WriteString("triangles\n");
			for (size_t itriangle = 0; itriangle < triangles.size(); itriangle++)
			{
				const Triangle& triangle = triangles[itriangle];

				textBuffer.WriteFormatted("%s\n", triangle.material);

				textBuffer.WriteFormatted("\t%s\n", vertexLines[triangle.vertices[0]]);
				textBuffer.WriteFormatted("\t%s\n", vertexLines[triangle.vertices[1]]);
				textBuffer.WriteFormatted("\t%s\n", vertexLines[triangle.vertices[2]]);
			}
			textBuffer.WriteString("end\n");
		}

		std::filesystem::path outPath(exportPath);
		outPath.append(exportName);
		outPath.replace_extension(".smd");

#ifndef STREAMIO
		std::ofstream file(outPath);
		file.write(textBuffer.Text(), textBuffer.TextLength());
#else
		StreamIO file(outPath, eStreamIOMode::Write);
		file.write(textBuffer.Text(), textBuffer.TextLength());
#endif // !STREAMIO

		return true;
	}
}