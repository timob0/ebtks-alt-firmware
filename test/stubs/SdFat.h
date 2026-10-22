#pragma once
// Native-only mock of the SdFat API used by CPM85 disk-image creation.
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum { O_RDONLY=0, O_RDWR=2, O_CREAT=0x200, O_EXCL=0x800 };
struct MockImage { std::vector<uint8_t> bytes; };
struct MockSd;
extern MockSd SD;
struct FsFile {
  std::shared_ptr<MockImage> image;
  size_t position = 0;
  bool opened = false;
  FsFile() = default;
  explicit FsFile(std::shared_ptr<MockImage> x):image(x),position(0),opened(true){}
  explicit operator bool() const { return opened && bool(image); }
  size_t write(const uint8_t *p, size_t n);
  bool sync() {return opened;}
  void close(){opened=false;}
  uint32_t size() const {return image ? uint32_t(image->bytes.size()) : 0;}
};
struct MockSd {
  std::unordered_map<std::string,std::shared_ptr<MockImage>> entries;
  size_t writtenTotal=0;
  size_t failAt=SIZE_MAX;
  bool exists(const char *p) const {return entries.find(p)!=entries.end();}
  FsFile open(const char *p, int flags) {
    auto it=entries.find(p);
    if(it!=entries.end() && (flags & O_EXCL))return {};
    if(it==entries.end()) {
      if(!(flags & O_CREAT))return {};
      it=entries.emplace(p,std::make_shared<MockImage>()).first;
    }
    return FsFile(it->second);
  }
  bool remove(const char *p) {return entries.erase(p)!=0;}
};
inline MockSd SD;
inline size_t FsFile::write(const uint8_t *p,size_t n) {
  if(!opened || SD.writtenTotal+n>SD.failAt)return 0;
  if(position+n>image->bytes.size())image->bytes.resize(position+n);
  std::memcpy(image->bytes.data()+position,p,n);
  position+=n;
  SD.writtenTotal+=n;
  return n;
}
