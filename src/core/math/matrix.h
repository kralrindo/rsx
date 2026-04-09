#pragma once

struct float3x4
{
	float m[3][4];
};

struct matrix3x4_t
{
	float* operator[](int i) { assert((i >= 0) && (i < 3)); return m_flMatVal[i]; }
	const float* operator[](int i) const { assert((i >= 0) && (i < 3)); return m_flMatVal[i]; }
	float* Base() { return &m_flMatVal[0][0]; }
	const float* Base() const { return &m_flMatVal[0][0]; }

	static matrix3x4_t Identity() {
		return matrix3x4_t{
			 1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
		};
	}

	std::string AsString() const {
		return std::format(
			"{} {} {} {}\n"
			"{} {} {} {}\n"
			"{} {} {} {}",
			m_flMatVal[0][0], m_flMatVal[0][1], m_flMatVal[0][2], m_flMatVal[0][3],
			m_flMatVal[1][0], m_flMatVal[1][1], m_flMatVal[1][2], m_flMatVal[1][3],
			m_flMatVal[2][0], m_flMatVal[2][1], m_flMatVal[2][2], m_flMatVal[2][3]);
	}

	float m_flMatVal[3][4];
};


void MatrixAngles(const matrix3x4_t& matrix, float* angles); // !!!!
void MatrixInvert(const matrix3x4_t& in, matrix3x4_t& out);

void MatrixGetColumn(const matrix3x4_t& in, int column, Vector& out);
void MatrixSetColumn(const Vector& in, int column, matrix3x4_t& out);

// https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/public/mathlib/mathlib.h#L857
inline void MatrixPosition(const matrix3x4_t& matrix, Vector& position)
{
	MatrixGetColumn(matrix, 3, position);
}

// https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/public/mathlib/mathlib.h#L872
inline void MatrixAngles(const matrix3x4_t& matrix, QAngle& angles)
{
	MatrixAngles(matrix, &angles.x);
}

inline void MatrixAngles(const matrix3x4_t& matrix, QAngle& angles, Vector& position)
{
	MatrixAngles(matrix, angles);
	MatrixPosition(matrix, position);
}

inline void MatrixAngles(const matrix3x4_t& matrix, RadianEuler& angles)
{
	MatrixAngles(matrix, &angles.x);

	angles.Init(DEG2RAD(angles.z), DEG2RAD(angles.x), DEG2RAD(angles.y));
}

void MatrixAngles(const matrix3x4_t& mat, RadianEuler& angles, Vector& position);
void MatrixAngles(const matrix3x4_t& mat, Quaternion& q, Vector& position);