#include <cassert>
#include "File.hpp"
#include "Wz.hpp"
#include "Directory.hpp"
#include "Keys.hpp"
#include "AES.h"

wz::File::File(const std::initializer_list<u8>& new_iv, const char* path)
    : reader(Reader(path)), root(new Node(Type::NotSet, reader)), key(new u8[0x10000]), iv(nullptr) {
    iv = new u8[4];
    memcpy(iv, new_iv.begin(), 4);
    init_key();
    reader.set_key(key);
}

wz::File::File(u8* new_iv, const char* path)
    : reader(Reader(path)), root(new Node(Type::NotSet, reader)), key(new u8[0x10000]), iv(new_iv) {
    init_key();
    reader.set_key(key);
}

#ifdef _WIN32
wz::File::File(const std::initializer_list<u8>& new_iv, const wchar_t* path)
    : reader(Reader(path)), root(new Node(reader)), key(new u8[0x10000]), iv(nullptr) {
    iv = new u8[4];
    memcpy(iv, new_iv.begin(), 4);
    init_key();
    reader.set_key(key);
}

wz::File::File(u8* new_iv, const wchar_t* path)
    : reader(Reader(path)), root(new Node(reader)), key(new u8[0x10000]), iv(new_iv) {
    init_key();
    reader.set_key(key);
}
#endif

wz::File::~File() {
    delete[] iv;
    delete[] key;
    delete root;
}

bool wz::File::parse() {
    auto magic = reader.read_string(4);
    if (magic != L"PKG1") return false;

    auto fileSize = reader.read<u64>();
    auto startAt = reader.read<u32>();

    auto copyright = reader.read_string();

    reader.set_position(startAt);

    auto encryptedVersion = reader.read<i16>();

    for (int i = 0; i < 0x7FFF; ++i) {
        i16 file_version = static_cast<decltype(file_version)>(i);
        u32 version_hash = wz::get_version_hash(encryptedVersion, file_version);

        if (version_hash != 0) {
            desc.start = startAt;
            desc.hash = version_hash;
            desc.version = file_version;

            auto prev_position = reader.get_position();

            if (!parse_directories(nullptr)) {
                reader.set_position(prev_position);
                continue;
            } else {
                if (root) {
                    reader.set_position(prev_position);
                    parse_directories(root);
                }
                return true;
            }
        }
    }

    return false;

}

bool wz::File::parse_directories(wz::Node* node) {
    auto entry_count = reader.read_compressed_int();

    for (int i = 0; i < entry_count; ++i) {
        auto type = reader.read_byte();
        size_t prevPos = 0;
        std::wstring name;

        if (type == 1) {
            reader.skip(sizeof(i32) + sizeof(u16));

            get_wz_offset();
            continue;
        } else if (type == 2) {
            i32 stringOffset = reader.read<i32>();
            type = reader.read_wz_string_from_offset<u8>(desc.start + stringOffset, name);
        } else if (type == 3 || type == 4) {
            name = reader.read_wz_string();
        } else {
            assert(0);
        }

        i32 size = reader.read_compressed_int();
        i32 checksum = reader.read_compressed_int();
        u32 offset = get_wz_offset();

        if (node == nullptr && offset >= reader.size())
            return false;

        if (type == 3) {
            if (node != nullptr) {
                auto* dir = new Directory(reader, false, size, checksum, offset);
                node->appendChild({name.begin(), name.end()}, dir);
            }
        } else {
            if (node != nullptr) {
                auto* dir = new Directory(reader, true, size, checksum, offset);
                node->appendChild({name.begin(), name.end()}, dir);
            } else {
                prevPos = reader.get_position();
                reader.set_position(offset);

                if (!reader.is_wz_image())
                    return false;

                reader.set_position(prevPos);
            }
        }
    }

    if (node != nullptr) {
        for (auto& it : *node) {
            for (auto child : it.second) {
                auto* dir = dynamic_cast<Directory*>(child);

                if (dir != nullptr) {
                    if (!dir->is_image()) {
                        reader.set_position(dir->get_offset());
                        parse_directories(dir);
                    }
                }
            }
        }
    }

    return true;
}

u32 wz::File::get_wz_offset() {
    u32 offset = static_cast<u32>(reader.get_position());
    offset = ~(offset - desc.start);
    offset *= desc.hash;
    offset -= wz::OffsetKey;
    offset = (offset << (offset & 0x1Fu)) | (offset >> (32 - (offset & 0x1Fu)));
    u32 encryptedOffset = reader.read<u32>();
    offset ^= encryptedOffset;
    offset += desc.start * 2;
    return offset;
}

wz::Node* wz::File::get_root() const {
    return root;
}

void wz::File::init_key() {
    AES AESGen;
    u8 biv[16];

    for (int i = 0; i < 16; i += 4) {
        memmove(biv + i, iv, 4);
    }

    AESGen.SetParameters(256, 128);
    AESGen.StartEncryption(wz::AesKey2);
    AESGen.EncryptBlock(biv, key);

    for (int i = 16; i < 0x10000; i += 16) {
        AESGen.EncryptBlock(key + i - 16, key + i);
    }
}

u8* wz::File::get_key() const {
    return key;
}