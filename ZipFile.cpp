#include "ZipFile.h"

#include "utils/stream_utils.h"

#include <fstream>
#include <cassert>
#include <stdexcept>

namespace {
std::string GetFilenameFromPath(const std::filesystem::path& fullPath) {
  return fullPath.filename().u8string();
}

std::string MakeTempFilename(const std::filesystem::path& fileName) {
  const auto temp = fileName.filename().wstring() + L".tmp";
  const auto path = std::filesystem::path(fileName).remove_filename() / temp;
  return path.u8string();
}
}  // namespace

ZipArchive::Ptr ZipFile::Open(const std::filesystem::path& zipPath) {
  std::ifstream* zipFile = new std::ifstream();
  zipFile->open(zipPath, std::ios::binary);

  if (!zipFile->is_open())
  {
    // if file does not exist, try to create it
    std::ofstream tmpFile;
    tmpFile.open(zipPath, std::ios::binary);
    tmpFile.close();

    zipFile->open(zipPath, std::ios::binary);

    // if attempt to create file failed, throw an exception
    if (!zipFile->is_open())
    {
      throw std::runtime_error("cannot open zip file");
    }
  }

  return ZipArchive::Create(zipFile, true);
}

void ZipFile::Save(ZipArchive::Ptr zipArchive,
    const std::filesystem::path& zipPath) {
  ZipFile::SaveAndClose(zipArchive, zipPath);

  zipArchive = ZipFile::Open(zipPath);
}

void ZipFile::SaveAndClose(ZipArchive::Ptr zipArchive,
    const std::filesystem::path& zipPath) {
  // check if file exist
  std::string tempZipPath = MakeTempFilename(zipPath);
  std::ofstream outZipFile;
  outZipFile.open(tempZipPath, std::ios::binary | std::ios::trunc);

  if (!outZipFile.is_open())
  {
    throw std::runtime_error("cannot save zip file");
  }

  zipArchive->WriteToStream(outZipFile);
  outZipFile.close();

  zipArchive->InternalDestroy();

  std::error_code ec;
  std::filesystem::remove(zipPath, ec);
  std::filesystem::rename(tempZipPath, zipPath, ec);
}

bool ZipFile::IsInArchive(const std::filesystem::path& zipPath,
    const std::string& fileName) {
  ZipArchive::Ptr zipArchive = ZipFile::Open(zipPath);
  return zipArchive->GetEntry(fileName) != nullptr;
}

void ZipFile::AddFile(const std::filesystem::path& zipPath,
    const std::string& fileName,
    ICompressionMethod::Ptr method) {
  AddFile(zipPath, fileName, GetFilenameFromPath(fileName), method);
}

void ZipFile::AddFile(const std::filesystem::path& zipPath,
    const std::string& fileName,
    const std::string& inArchiveName,
    ICompressionMethod::Ptr method) {
  AddEncryptedFile(zipPath, fileName, inArchiveName, std::string(), method);
}

void ZipFile::AddEncryptedFile(const std::filesystem::path& zipPath,
    const std::string& fileName,
    const std::string& password,
    ICompressionMethod::Ptr method) {
  AddEncryptedFile(zipPath, fileName, GetFilenameFromPath(fileName), std::string(), method);
}

void ZipFile::AddEncryptedFile(const std::filesystem::path& zipPath,
    const std::string& fileName,
    const std::string& inArchiveName,
    const std::string& password,
    ICompressionMethod::Ptr method) {
  std::string tmpName = MakeTempFilename(zipPath);

  {
    ZipArchive::Ptr zipArchive = ZipFile::Open(zipPath);

    std::ifstream fileToAdd;
    fileToAdd.open(fileName, std::ios::binary);

    if (!fileToAdd.is_open())
    {
      throw std::runtime_error("cannot open input file");
    }

    auto fileEntry = zipArchive->CreateEntry(inArchiveName);

    if (fileEntry == nullptr)
    {
      //throw std::runtime_error("input file already exist in the archive");
      zipArchive->RemoveEntry(inArchiveName);
      fileEntry = zipArchive->CreateEntry(inArchiveName);
    }

    if (!password.empty())
    {
      fileEntry->SetPassword(password);
      fileEntry->UseDataDescriptor();
    }

    fileEntry->SetCompressionStream(fileToAdd, method);

    //////////////////////////////////////////////////////////////////////////

    std::ofstream outFile;
    outFile.open(tmpName, std::ios::binary);

    if (!outFile.is_open())
    {
      throw std::runtime_error("cannot open output file");
    }

    zipArchive->WriteToStream(outFile);
    outFile.close();
  
    // force closing the input zip stream
  }

  std::error_code ec;
  std::filesystem::remove(zipPath, ec);
  std::filesystem::rename(tmpName, zipPath, ec);
}

void ZipFile::ExtractFile(const std::filesystem::path& zipPath,
    const std::string& fileName) {
  ExtractFile(zipPath, fileName, GetFilenameFromPath(fileName));
}

void ZipFile::ExtractFile(const std::filesystem::path& zipPath,
    const std::string& fileName,
    const std::string& destinationPath) {
  ExtractEncryptedFile(zipPath, fileName, destinationPath, std::string());
}

void ZipFile::ExtractEncryptedFile(const std::filesystem::path& zipPath,
    const std::string& fileName,
    const std::string& password) {
  ExtractEncryptedFile(zipPath, fileName, GetFilenameFromPath(fileName), password);
}

void ZipFile::ExtractEncryptedFile(const std::filesystem::path& zipPath,
    const std::string& fileName,
    const std::string& destinationPath,
    const std::string& password) {
  ZipArchive::Ptr zipArchive = ZipFile::Open(zipPath);

  std::ofstream destFile;
  destFile.open(destinationPath, std::ios::binary | std::ios::trunc);

  if (!destFile.is_open())
  {
    throw std::runtime_error("cannot create destination file");
  }

  auto entry = zipArchive->GetEntry(fileName);

  if (entry == nullptr)
  {
    throw std::runtime_error("file is not contained in zip file");
  }

  if (!password.empty())
  {
    entry->SetPassword(password);
  }

  std::istream* dataStream = entry->GetDecompressionStream();

  if (dataStream == nullptr)
  {
    throw std::runtime_error("wrong password");
  }

  utils::stream::copy(*dataStream, destFile);

  destFile.flush();
  destFile.close();
}

void ZipFile::RemoveEntry(const std::filesystem::path& zipPath,
    const std::string& fileName) {
  std::string tmpName = MakeTempFilename(zipPath);

  {
    ZipArchive::Ptr zipArchive = ZipFile::Open(zipPath);
    zipArchive->RemoveEntry(fileName);

    //////////////////////////////////////////////////////////////////////////

    std::ofstream outFile;

    outFile.open(tmpName, std::ios::binary);

    if (!outFile.is_open())
    {
      throw std::runtime_error("cannot open output file");
    }

    zipArchive->WriteToStream(outFile);
    outFile.close();

    // force closing the input zip stream
  }

  std::error_code ec;
  std::filesystem::remove(zipPath, ec);
  std::filesystem::rename(tmpName, zipPath, ec);
}
