//
//  Ripe.cc
//
//  Copyright © 2017 Muflihun.com. All rights reserved.
//

#include <iomanip>
#include <memory>
#include <sstream>
#include <fstream>
#include <vector>

#include <cryptopp/osrng.h>
#include <cryptopp/base64.h>
#include <cryptopp/modes.h>
#include <cryptopp/hex.h>
#include <cryptopp/pem.h>
#include <cryptopp/rsa.h>

#include <zlib.h>

#include "include/Ripe.h"
#include "include/log.h"

#define RIPE_UNUSED(x) (void)x

INITIALIZE_EASYLOGGINGPP

using namespace CryptoPP;

const std::string Ripe::PACKET_DELIMITER = "\r\n\r\n";
const std::size_t Ripe::PACKET_DELIMITER_SIZE = Ripe::PACKET_DELIMITER.size();
const char Ripe::DATA_DELIMITER = ':';
const int Ripe::BITS_PER_BYTE = 8;
const int Ripe::DEFAULT_RSA_LENGTH = 2048;
const int Ripe::ZLIB_BUFFER_SIZE = 32768;
const int Ripe::AES_BSIZE = AES::BLOCKSIZE;
const std::string Ripe::BASE64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

bool loadPrivateKey(const std::string& key, RSA::PrivateKey& keyOut, const std::string& secret)
{
    StringSource source(key, true);
    if (secret.empty()) {
        PEM_Load(source, keyOut);
    } else {
        PEM_Load(source, keyOut, secret.data(), secret.size());
    }
    AutoSeededRandomPool prng;
    return keyOut.Validate(prng, 3);
}

bool loadPublicKey(const std::string& key, RSA::PublicKey& keyOut)
{
    StringSource source(key, true);
    PEM_Load(source, keyOut);
    AutoSeededRandomPool prng;
    return keyOut.Validate(prng, 3);
}

std::string Ripe::encryptRSA(const std::string& data, const std::string& publicKeyPEM)
{
    RSA::PublicKey publicKey;
    bool rsaKeyValid = loadPublicKey(publicKeyPEM, publicKey);
    if (!rsaKeyValid) {
        throw std::invalid_argument("Could not load public key");
    }
    RSAES_PKCS1v15_Encryptor e(publicKey);
    std::string result;
    AutoSeededRandomPool rng;
    StringSource ss(data, true,
        new PK_EncryptorFilter(rng, e,
            new StringSink(result)
       )
    );
    RIPE_UNUSED(ss);
    return result;
}

std::string Ripe::encryptRSA(std::string& data, const std::string& key, const std::string& outputFile, bool isRaw)
{
    try {
        std::string encryptedData = Ripe::encryptRSA(data, key);
        if (!isRaw) {
            encryptedData = Ripe::base64Encode(encryptedData);
        }
        if (!outputFile.empty()) {
            std::ofstream out(outputFile);
            out.write(encryptedData.c_str(), encryptedData.size());
            out.flush();
            out.close();
            return "";
        }
        return encryptedData;
    } catch (const std::exception& e) {
        throw e;
    }
}

std::string Ripe::decryptRSA(const std::string& data, const std::string& privateKeyPEM, const std::string& secret)
{
    RSA::PrivateKey privateKey;
    bool rsaKeyValid = loadPrivateKey(privateKeyPEM, privateKey, secret);
    if (!rsaKeyValid) {
        throw std::invalid_argument("Could not load private key");
    }

    std::string result;
    AutoSeededRandomPool rng;
    RSAES_PKCS1v15_Decryptor d(privateKey);

    StringSource ss(data, true,
        new PK_DecryptorFilter(rng, d,
            new StringSink(result)
       )
    );
    RIPE_UNUSED(ss);
    return result;
}

std::string Ripe::decryptRSA(std::string& data, const std::string& key, bool isBase64, bool isHex, const std::string& secret)
{
    if (isBase64) {
        data = Ripe::base64Decode(data);
    }

    if (isHex) {
        data = Ripe::hexToString(data);
    }
    return Ripe::decryptRSA(data, key, secret);
}

bool Ripe::verifyRSA(const std::string& data, const std::string& signatureHex, const std::string& publicKeyPEM)
{
    RSA::PublicKey publicKey;
    bool rsaKeyValid = loadPublicKey(publicKeyPEM, publicKey);
    if (!rsaKeyValid) {
        throw std::invalid_argument("Could not load public key");
    }
    std::string decodedSignature = Ripe::hexToString(signatureHex);
    bool result = false;
    RSASSA_PKCS1v15_SHA_Verifier verifier(publicKey);
    StringSource ss2(decodedSignature + data, true,
                     new SignatureVerificationFilter(verifier,
                                                     new ArraySink((byte*)&result, sizeof(result))));
    return result;
}

std::string Ripe::signRSA(const std::string& data, const std::string& privateKeyPEM, const std::string& privateKeySecret)
{
    RSA::PrivateKey privateKey;
    bool rsaKeyValid = loadPrivateKey(privateKeyPEM, privateKey, privateKeySecret);
    if (!rsaKeyValid) {
        throw std::invalid_argument("Could not load private key");
    }

    // sign message
    std::string signature;
    RSASSA_PKCS1v15_SHA_Signer signer(privateKey);
    AutoSeededRandomPool rng;

    StringSource ss(data, true,
                    new SignerFilter(rng, signer,
                                     new HexEncoder(
                                         new StringSink(signature))));
    return signature;
}

bool Ripe::writeRSAKeyPair(const std::string& publicFile, const std::string& privateFile, int length)
{
    RLOG(INFO) << "Generating key pair that can encrypt " << Ripe::maxRSABlockSize(length) << " bytes";
    bool result = true;
    KeyPair keypair = Ripe::generateRSAKeyPair(length);
    if (keypair.privateKey.size() > 0 && keypair.publicKey.size() > 0) {
        std::ofstream fs(privateFile.c_str(), std::ios::out);
        if (fs.is_open()) {
            fs.write(keypair.privateKey.c_str(), keypair.privateKey.size());
            fs.close();
        } else {
            RLOG(ERROR) << "Unable to open [" << privateFile << "]";
            result = false;
        }
        fs.open(publicFile.c_str(), std::ios::out);
        if (fs.is_open()) {
            fs.write(keypair.publicKey.c_str(), keypair.publicKey.size());
            fs.close();
            result = result && true;
        } else {
            RLOG(ERROR) << "Unable to open [" << publicFile << "]";
            result = false;
        }
    }
    if (!result) {
        RLOG(ERROR) << "Failed to generate key pair! Please check logs for details" << std::endl;
        throw std::logic_error("Failed to generate key pair!");
    }
    RLOG(INFO) << "Successfully saved!";
    return result;
}

std::string Ripe::generateRSAKeyPairBase64(int length)
{
    Ripe::KeyPair pair = Ripe::generateRSAKeyPair(length);
    if (pair.privateKey.empty() || pair.publicKey.empty()) {
        RLOG(ERROR) << "Failed to generate key pair! Please check logs for details" << std::endl;
        throw std::logic_error("Failed to generate key pair!");
    }
    return Ripe::base64Encode(pair.privateKey) + ":" + Ripe::base64Encode(pair.publicKey);
}

Ripe::KeyPair Ripe::generateRSAKeyPair(unsigned int length)
{
    AutoSeededRandomPool rng;
    InvertibleRSAFunction params;
    params.GenerateRandomWithKeySize(rng, length);
    RSA::PrivateKey privateKey(params);
    RSA::PublicKey publicKey(params);

    Ripe::KeyPair pair;
    {
        StringSink snk(pair.privateKey);
        PEM_Save(snk, privateKey);
        snk.MessageEnd();
    }
    {
        StringSink snk(pair.publicKey);
        PEM_Save(snk, publicKey);
        snk.MessageEnd();
    }
    return pair;
}

std::string Ripe::base64Encode(const std::string& input)
{
    std::string encoded;
    StringSource ss(input, true, new Base64Encoder(
                        new StringSink(encoded), false /* insert line breaks */)
                    );
    RIPE_UNUSED(ss);
    return encoded;
}

std::string Ripe::base64Decode(const std::string& base64Encoded)
{
    std::string decoded;
    StringSource ss(base64Encoded, true, new Base64Decoder(
                        new StringSink(decoded))
                    );
    RIPE_UNUSED(ss);
    return decoded;
}

std::string Ripe::generateNewKey(int length)
{
    if (!(length == 16 || length == 24 || length == 32)) {
        throw std::invalid_argument( "Invalid key length. Acceptable lengths are 16, 24 or 32" );
    }
    AutoSeededRandomPool rnd;
    SecByteBlock key(length);
    rnd.GenerateBlock(key, key.size());
    std::string s;
    HexEncoder hex(new StringSink(s));
    hex.Put(key.data(), key.size());
    hex.MessageEnd();
    return s;
}

std::string Ripe::encryptAES(const std::string& buffer, const byte* key, std::size_t keySize, std::vector<byte>& iv)
{
    SecByteBlock keyBlock(key, keySize);

    byte ivArr[Ripe::AES_BSIZE] = {0};

    AutoSeededRandomPool rnd;
    rnd.GenerateBlock(ivArr, sizeof ivArr);

    std::string cipher;

    CBC_Mode<AES>::Encryption e;
    e.SetKeyWithIV(keyBlock, keyBlock.size(), ivArr);

    // store for user
    iv.resize(sizeof ivArr);
    std::copy(std::begin(ivArr), std::end(ivArr), iv.begin());

    // The StreamTransformationFilter adds padding as required.
    StringSource ss(buffer, true,
                    new StreamTransformationFilter(e, new StringSink(cipher))
                    );
    RIPE_UNUSED(ss);
    return cipher;
}

std::string Ripe::encryptAES(std::string& data, const std::string& hexKey, const std::string& clientId, const std::string& outputFile)
{
    std::stringstream ss;
    if (!outputFile.empty()) {
        std::vector<byte> iv;
        std::string encrypted = Ripe::encryptAES(data, hexKey, iv);

        std::ofstream out(outputFile);
        out << encrypted.data();
        out.close();
        ss << "IV: " << std::hex << std::setfill('0');
        for (byte b : iv) {
            ss << std::setw(2) << static_cast<unsigned int>(b);
        }
        ss << std::endl;
    } else {
        ss << Ripe::prepareData(data, hexKey, clientId.c_str());
    }
    return ss.str();
}

std::string Ripe::decryptAES(const std::string& data, const byte* key, std::size_t keySize, std::vector<byte>& iv)
{
    std::string result;
    SecByteBlock keyBlock(key, keySize);

    byte ivArr[Ripe::AES_BSIZE] = {0};
    std::copy(iv.begin(), iv.end(), std::begin(ivArr));

    CBC_Mode<AES>::Decryption d;
    d.SetKeyWithIV(keyBlock, keyBlock.size(), ivArr);

    StringSource ss(data, true,
                new StreamTransformationFilter( d, new StringSink(result))
                );
    RIPE_UNUSED(ss);
    return result;
}

std::string Ripe::decryptAES(std::string& data, const std::string& hexKey, std::string& ivec, bool isBase64, bool isHex)
{
    if (ivec.empty() && isBase64) {
        // Extract IV from data
        std::size_t pos = data.find_first_of(':');
        if (pos == 32) {
            ivec = data.substr(0, pos);
            Ripe::normalizeHex(ivec);
            data = data.substr(pos + 1);
            pos = data.find_first_of(':');
            if (pos != std::string::npos) {
                // We ignore clientId which is = data.substr(0, pos);
                data = data.substr(pos + 1);
            }
        }
    }
    if (ivec.size() == 32) {
        // Condensed form needs to be normalized
        Ripe::normalizeHex(ivec);
    }

    byte* iv = reinterpret_cast<byte*>(const_cast<char*>(ivec.data()));
    std::vector<byte> ivHex = Ripe::byteToVec(iv);

    if (isBase64) {
        data = Ripe::base64Decode(data);
    }
    if (isHex) {
        data = Ripe::hexToString(data);
    }
    return Ripe::decryptAES(data, reinterpret_cast<const byte*>(Ripe::hexToString(hexKey).c_str()), hexKey.size() / 2, ivHex);
}

bool Ripe::compressFile(const std::string& gzFilename, const std::string& inputFile) noexcept
{
    gzFile out = gzopen(gzFilename.c_str(), "wb");
    if (!out) {
        RLOG(ERROR) << "Unable to open file [" << gzFilename << "] for writing." << std::strerror(errno);
        return false;
     }
    char buff[BUFSIZ];
    std::FILE* in = std::fopen(inputFile.c_str(), "rb");
    std::size_t nRead = 0;
    while((nRead = std::fread(buff, sizeof(char), BUFSIZ, in)) > 0) {
        int bytes_written = gzwrite(out, buff, nRead);
        if (bytes_written == 0) {
           int err_no = 0;
           RLOG(ERROR) << "Error during compression: " << gzerror(out, &err_no);
           gzclose(out);
           return false;
        }
    }
    gzclose(out);
    std::fclose(in);
    return true;
}

std::string Ripe::compressString(const std::string& str)
{
    int compressionlevel = Z_BEST_COMPRESSION;
    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    if (deflateInit(&zs, compressionlevel) != Z_OK) {
        throw std::runtime_error("Unable to initialize zlib deflate");
    }

    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(str.data()));
    zs.avail_in = str.size();

    int ret;
    char outbuffer[ZLIB_BUFFER_SIZE];
    std::string outstring;

    // retrieve the compressed bytes blockwise
    do {
        zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
        zs.avail_out = sizeof(outbuffer);

        ret = deflate(&zs, Z_FINISH);

        if (outstring.size() < zs.total_out) {
            outstring.append(outbuffer, zs.total_out - outstring.size());
        }
    } while (ret == Z_OK);

    deflateEnd(&zs);

    if (ret != Z_STREAM_END) {
        std::ostringstream oss;
        oss << "Exception during zlib compression: (" << ret << ") " << zs.msg;
        throw std::runtime_error(oss.str());
    }

    return outstring;
}

std::string Ripe::decompressString(const std::string& str)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    if (inflateInit(&zs) != Z_OK) {
        throw std::runtime_error("Unable to initialize zlib inflate");
    }

    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(str.data()));
    zs.avail_in = str.size();

    int ret;
    char outbuffer[ZLIB_BUFFER_SIZE];
    std::string outstring;

    do {
        zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
        zs.avail_out = sizeof(outbuffer);

        ret = inflate(&zs, 0);

        if (outstring.size() < zs.total_out) {
            outstring.append(outbuffer, zs.total_out - outstring.size());
        }

    } while (ret == Z_OK);

    inflateEnd(&zs);

    if (ret != Z_STREAM_END) {
        std::ostringstream oss;
        oss << "Exception during zlib decompression: (" << ret << ") " << zs.msg;
        throw std::runtime_error(oss.str());
    }

    return outstring;
}

std::string Ripe::prepareData(const std::string& data, const std::string& hexKey, const char* clientId)
{
    std::vector<byte> iv;
    std::string encrypted = Ripe::encryptAES(data, hexKey, iv);
    // Encryption Base64 encoding
    std::string base64Encoded = Ripe::base64Encode(encrypted);

    // IV Hex
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (byte b : iv) {
        ss << std::setw(2) << static_cast<unsigned int>(b);
    }
    ss << Ripe::DATA_DELIMITER;
    if (strlen(clientId) > 0) {
        ss << clientId << Ripe::DATA_DELIMITER;
    }
    ss << base64Encoded;
    std::stringstream fss;
    fss << ss.str() << PACKET_DELIMITER;
    return fss.str();
}

bool Ripe::normalizeHex(std::string& iv) noexcept
{
    if (iv.size() == 32) {
        for (int j = 2; j < 32 + 15; j += 2) {
            iv.insert(j, " ");
            j++;
        }
        return true;
    }
    return false;
}

std::string Ripe::vecToString(const std::vector<byte>& iv) noexcept
{
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (byte b : iv) {
        ss << std::setw(2) << static_cast<unsigned int>(b);
    }
    return ss.str();
}

std::vector<byte> Ripe::byteToVec(const byte* b) noexcept
{
    std::vector<byte> hexData;

    std::istringstream ss(reinterpret_cast<const char*>(b));

    unsigned int c;
    while (ss >> std::hex >> c) {
        hexData.push_back(c);
    }
    return hexData;
}

std::string Ripe::hexToString(const std::string& hex)
{
    std::string result;
    StringSource ss(hex, true,
        new HexDecoder(
            new StringSink(result)
        )
    );
    RIPE_UNUSED(ss);
    return result;
}

std::string Ripe::stringToHex(const std::string& raw) noexcept
{
    std::string result;
    StringSource ss(raw, true,
        new HexEncoder(
            new StringSink(result)
        )
    );
    RIPE_UNUSED(ss);
    return result;
}

std::size_t Ripe::expectedDataSize(std::size_t plainDataSize, std::size_t clientIdSize) noexcept
{
    std::size_t dataSize = 32 /* IV */
            + sizeof(DATA_DELIMITER) /* : */
            + (clientIdSize > 0 ? clientIdSize + sizeof(DATA_DELIMITER) : 0)
            + expectedBase64Length(expectedAESCipherLength(plainDataSize));
    return dataSize + PACKET_DELIMITER_SIZE;
}

std::string Ripe::version() noexcept
{
    return RIPE_VERSION;
}
