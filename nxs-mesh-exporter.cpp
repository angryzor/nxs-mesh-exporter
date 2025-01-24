#include <iostream>
#include <fstream>
#include <string>
#include <bit>
#include <algorithm>
#include <span>
#include <filesystem>

struct Vertex { float x; float y; float z; };
template<typename T>
struct Triangle { T i; T j; T k; };

#ifndef __cpp_lib_byteswap
namespace std {
	// Reference implementation from cppreference.com
	template<integral T> constexpr T byteswap(T value) noexcept
	{
		static_assert(has_unique_object_representations_v<T>, "T may not have padding bits");
		auto value_representation = bit_cast<array<byte, sizeof(T)>>(value);
		ranges::reverse(value_representation);
		return bit_cast<T>(value_representation);
	}
}
#endif

namespace util {
	template<typename T> inline T byteswap(T value) noexcept { static_assert("invalid byteswap"); }
	template<std::integral T> inline T byteswap(T value) noexcept { return std::byteswap(value); }
	template<> inline unsigned long long byteswap(unsigned long long value) noexcept { return _byteswap_uint64(value); }
	template<> inline unsigned int byteswap(unsigned int value) noexcept { return _byteswap_ulong(value); }
	template<> inline unsigned short byteswap(unsigned short value) noexcept { return _byteswap_ushort(value); }
	template<> inline double byteswap(double value) noexcept { return std::bit_cast<double>(_byteswap_uint64(std::bit_cast<unsigned long long>(value))); }
	template<> inline float byteswap(float value) noexcept { return std::bit_cast<float>(_byteswap_ulong(std::bit_cast<unsigned int>(value))); }

	template<std::integral T>
	T byteswap_to_native(std::endian endianness, T value) noexcept {
		return std::endian::native != endianness ? byteswap(value) : value;
	}

	template<typename T> inline void byteswap_deep(T& value) noexcept { value.byteswap_deep(); }
	template<std::integral T> inline void byteswap_deep(T& value) noexcept { value = byteswap(value); }
	template<std::floating_point T> inline void byteswap_deep(T& value) noexcept { value = byteswap(value); }
	template<> inline void byteswap_deep(uint8_t& value) noexcept { }

	template<>
	inline void byteswap_deep(Vertex& value) noexcept {
		byteswap_deep(value.x);
		byteswap_deep(value.y);
		byteswap_deep(value.z);
	}

	template<typename T>
	inline void byteswap_deep(Triangle<T> &value) noexcept {
		byteswap_deep(value.i);
		byteswap_deep(value.j);
		byteswap_deep(value.k);
	}

	template<typename T>
	inline void byteswap_deep_to_native(std::endian endianness, T& value) noexcept {
		if (std::endian::native != endianness)
			byteswap_deep(value);
	}
}
namespace internal {
	char zeroes[8192]{};
}

template<typename T, typename A>
inline T align(T addr, A alignment) {
	return (T)(((size_t)addr + alignment - 1) & ~(alignment - 1));
}

template<typename T>
inline T* addptr(T* addr, size_t off) {
	return reinterpret_cast<T*>(reinterpret_cast<size_t>(addr) + off);
}

class fast_istream {
	std::istream& stream;
	size_t shadow_pos; // prevent doing slow tellg() calls.

public:
	fast_istream(std::istream& stream) : stream{ stream }, shadow_pos{ (size_t)stream.tellg() } {}

	void read(char* str, size_t count) {
		stream.read(str, count);
		shadow_pos += count;
	}

	void read_string(std::string& str) {
		std::getline(stream, str, '\0');
		shadow_pos += str.size() + 1;
	}

	void seekg(size_t loc) {
		stream.seekg(loc);
		shadow_pos = loc;
	}

	size_t tellg() const {
		return shadow_pos;
	}
};

class binary_istream {
protected:
	fast_istream& stream;
	size_t offset;

public:
	std::endian endianness;

	binary_istream(fast_istream& stream, std::endian endianness = std::endian::native, size_t offset = 0) : stream{ stream }, endianness{ endianness }, offset{ offset } {}

	template<typename T, bool byteswap = true>
	void read(T& obj) {
		stream.read(reinterpret_cast<char*>(&obj), sizeof(T));

		if constexpr (byteswap)
			util::byteswap_deep_to_native(endianness, obj);
	}

	void read_string(std::string& str) {
		stream.read_string(str);
	}

	void skip_padding(size_t alignment) {
		skip_padding_bytes(align(tellg(), alignment) - tellg());
	}

	void skip_padding_bytes(size_t size) {
		seekg(tellg() + size);
	}

	void seekg(size_t loc) {
		stream.seekg(loc + offset);
	}

	size_t tellg() const {
		return stream.tellg() - offset;
	}
};

int main(size_t argc, const char* argv[])
{
	std::ifstream ifs{ argv[1], std::ios::binary };
	fast_istream fis{ ifs };
	binary_istream bis{ fis, std::endian::little };

	unsigned int flags;
	bis.skip_padding_bytes(0xC);
	bis.read(flags);
	bis.skip_padding_bytes(0xC);
	unsigned int vertexCount;
	unsigned int triangleCount;

	bis.read(vertexCount);
	bis.read(triangleCount);


	auto vertices = std::make_unique<Vertex[]>(vertexCount);
	auto triangles = std::make_unique<Triangle<unsigned int>[]>(triangleCount);
	auto materials = std::make_unique<unsigned short[]>(triangleCount);

	for (unsigned int i = 0; i < vertexCount; i++)
		bis.read(vertices[i]);

	if (flags & 0x10) {
		for (unsigned int i = 0; i < triangleCount; i++) {
			Triangle<unsigned short> tri;
			bis.read(tri);
			triangles[i] = { tri.i, tri.j, tri.k };
		}
	}
	else if (flags & 0x8) {
		for (unsigned int i = 0; i < triangleCount; i++) {
			Triangle<uint8_t> tri;
			bis.read(tri);
			triangles[i] = { tri.i, tri.j, tri.k };
		}
	}
	else {
		for (unsigned int i = 0; i < triangleCount; i++)
			bis.read(triangles[i]);
	}

	for (unsigned int i = 0; i < triangleCount; i++)
		bis.read(materials[i]);

	std::filesystem::path stem{ argv[1] };
	while (stem.has_extension())
		stem = stem.replace_extension();

	std::ofstream ofs{ stem.generic_string() + ".obj"};

	for (auto& vtx : std::span(vertices.get(), vertexCount))
		ofs << "v " << vtx.x * 0.1f << " " << vtx.y * 0.1f << " " << vtx.z * 0.1f << std::endl;

	for (unsigned int i = 0; i < 10; i++) {
		ofs << "g " << "material_" << i << std::endl;

		for (unsigned int j = 0; j < triangleCount; j++) {
			auto& tri = triangles[j];
			auto& mat = materials[j];

			if (mat == i)
				ofs << "f " << tri.i + 1 << " " << tri.j + 1 << " " << tri.k + 1 << std::endl;
		}
	}
}
