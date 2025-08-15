#ifndef CPP_BASE_SAFE_BASE64_H_
#define CPP_BASE_SAFE_BASE64_H_
/*
URL安全的base64方法
模拟Python base64中的urlsafe_b64encode与urlsafe_b64decode方法
与Base64相比，只是将encode表的'+'替换为'-'，'/'替换为'_'
解码表D中D[45]->0x3E,D[95]=0x3F，同时将D[43]和D[47]置为0xFF
*/
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

class Base64Encrypt {
 public:
  Base64Encrypt() : group_length_(0) {}
  Base64Encrypt(const void *input, size_t length) : Base64Encrypt() {
    Update(input, length);
  }

  void Update(const void *input, size_t length) {
    static const size_t LEN = 3;
    buf_.reserve(buf_.size() + (length - (LEN - group_length_) + LEN - 1) / LEN * 4 + 1);
    const unsigned char *buf_f = reinterpret_cast<const unsigned char *>(input);
    unsigned int i;

    for (i = 0; i < length; ++i) {
      group_[group_length_++] = buf_f[i];
      if (group_length_ == LEN) {
        Encode();
      }
    }
  }
  const unsigned char *CipherText() {
    Final();
    return buf_.data();
  }
  std::string GetString() {
    const char *pstr = (const char *)CipherText();
    size_t length = GetSize();
    return std::string(pstr, length);
  }
  void Reset() {
    buf_.clear();
    group_length_ = 0;
    for (unsigned int i = 0; i < sizeof(group_) / sizeof(group_[0]); ++i) {
      group_[i] = 0;
    }
  }
  size_t GetSize() {
    CipherText();
    return buf_.size();
  }

 private:
  Base64Encrypt(const Base64Encrypt &) = delete;
  Base64Encrypt &operator=(const Base64Encrypt &) = delete;

  void Encode() {
    unsigned char index;

    // 0 index byte
    index = group_[0] >> 2;
    buf_.push_back(kBase64EncodeMap[index]);
    // 1 index byte
    index = ((group_[0] & 0x03) << 4) | (group_[1] >> 4);
    buf_.push_back(kBase64EncodeMap[index]);
    // 2 index byte
    index = ((group_[1] & 0x0F) << 2) | (group_[2] >> 6);
    buf_.push_back(kBase64EncodeMap[index]);
    // 3 index byte
    index = group_[2] & 0x3F;
    buf_.push_back(kBase64EncodeMap[index]);

    group_length_ = 0;
  }
  void Final() {
    unsigned char index;

    if (group_length_ == 1) {
      group_[1] = 0;
      // 0 index byte
      index = group_[0] >> 2;
      buf_.push_back(kBase64EncodeMap[index]);
      // 1 index byte
      index = ((group_[0] & 0x03) << 4) | (group_[1] >> 4);
      buf_.push_back(kBase64EncodeMap[index]);
      // 2 index byte
      buf_.push_back('=');
      // 3 index byte
      buf_.push_back('=');
    } else if (group_length_ == 2) {
      group_[2] = 0;
      // 0 index byte
      index = group_[0] >> 2;
      buf_.push_back(kBase64EncodeMap[index]);
      // 1 index byte
      index = ((group_[0] & 0x03) << 4) | (group_[1] >> 4);
      buf_.push_back(kBase64EncodeMap[index]);
      // 2 index byte
      index = ((group_[1] & 0x0F) << 2) | (group_[2] >> 6);
      buf_.push_back(kBase64EncodeMap[index]);
      // 3 index byte
      buf_.push_back('=');
    }

    group_length_ = 0;
  }

 private:
  std::vector<unsigned char> buf_;
  unsigned char group_[3];
  int group_length_;

  const unsigned char kBase64EncodeMap[64] =
  {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3',
    '4', '5', '6', '7', '8', '9', '-', '_'
  };
};

class Base64Decrypt {
 public:
  Base64Decrypt() : group_length_(0) {}
  Base64Decrypt(const void *input, size_t length) : Base64Decrypt() {
    Update(input, length);
  }

  void Update(const void *input, size_t length) {
    static const size_t LEN = 4;
    buf_.reserve(buf_.size() + (length + (LEN - group_length_) + LEN - 1) / LEN * 3 + 1);
    const unsigned char *buf_f = reinterpret_cast<const unsigned char *>(input);
    unsigned int i;

    for (i = 0; i < length; ++i) {
      if (kBase64DecodeMap[buf_f[i]] == 0xFF) {
        throw std::invalid_argument("ciphertext is illegal");
      }

      group_[group_length_++] = buf_f[i];
      if (group_length_ == LEN) {
        Decode();
      }
    }
  }

  const unsigned char *PlainText() {
    if (group_length_) {
      throw std::invalid_argument("ciphertext's length must be a multiple of 4");
    }
    return buf_.data();
  }
  void Reset() {
    buf_.clear();
    group_length_ = 0;
    for (unsigned int i = 0; i < sizeof(group_) / sizeof(group_[0]); ++i) {
      group_[i] = 0;
    }
  }
  size_t GetSize() {
    PlainText();
    return buf_.size();
  }
  std::string GetString() {
    const char *pstr = (const char *)PlainText();
    size_t length = GetSize();
    return std::string(pstr, length);
  }

 private:
  Base64Decrypt(const Base64Decrypt &) = delete;
  Base64Decrypt &operator=(const Base64Decrypt &) = delete;

  void Decode() {
    unsigned char buf_f[3];
    unsigned int top = 1;
    if (group_[0] == '=' || group_[1] == '=') {
      throw std::invalid_argument("ciphertext is illegal");
    }

    buf_f[0] = (kBase64DecodeMap[group_[0]] << 2) | (kBase64DecodeMap[group_[1]] >> 4);
    if (group_[2] != '=') {
      buf_f[1] = ((kBase64DecodeMap[group_[1]] & 0x0F) << 4) | (kBase64DecodeMap[group_[2]] >> 2);
      top = 2;
    }
    if (group_[3] != '=') {
      buf_f[2] = (kBase64DecodeMap[group_[2]] << 6) | kBase64DecodeMap[group_[3]];
      top = 3;
    }

    for (unsigned int i = 0; i < top; ++i) {
      buf_.push_back(buf_f[i]);
    }

    group_length_ = 0;
  }

 private:
  std::vector<unsigned char> buf_;
  unsigned char group_[4];
  int group_length_;

  const unsigned char kBase64DecodeMap[256] =
  {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3E, 0xFF, 0xFF,
    0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B,
    0x3C, 0x3D, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF,
    0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F,
    0xFF, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30,
    0x31, 0x32, 0x33, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
};

#endif