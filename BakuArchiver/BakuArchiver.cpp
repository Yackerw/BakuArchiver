#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <intrin.h>
#include <filesystem>
#include <string>
#include <vector>
namespace fs = std::filesystem;

unsigned int ReadBEInt32(FILE* f) {
    unsigned int tmp;
    fread(&tmp, 4, 1, f);
    return _byteswap_ulong(tmp);
}

void UnpackArchive(FILE* f, char* outputFolder) {
    // first, file count
    int FCount = ReadBEInt32(f) / 2;
    char* outputFile = (char*)malloc(strlen(outputFolder) + 128);
    // now iterate over every file start and extract it; file length can be extrapolated from next file, or the archive length
    for (int i = 0; i < FCount; ++i) {
        int fileOffs = ReadBEInt32(f);
        int fileSize = ReadBEInt32(f);
        // not every slot always gets a file!!
        if (fileOffs == 0) {
            continue;
        }
        int nextFile = ftell(f);
        fseek(f, fileOffs, SEEK_SET);
        // allocate the file size so we can read it, then store it to the folder
        char* newFile = (char*)malloc(fileSize);
        sprintf(outputFile, "%s%i", outputFolder, i);
        FILE* output = fopen(outputFile, "wb");
        if (output != NULL) {
            // lol silently fail i guess
            fread(newFile, fileSize, 1, f);
            fwrite(newFile, fileSize, 1, output);
            fclose(output);
        }
        free(newFile);
        fseek(f, nextFile, SEEK_SET);
    }
    free(outputFile);
}

void AddToFileLE32(char* fData, int offs, int value) {
    int tmp = _byteswap_ulong(value);
    memcpy(fData + offs, &tmp, 4);
}

void PackArchive(char* inputFolder, char* outputFilePath) {
    std::vector<FILE*> files;
    std::vector<int> fileInd;
    // get every file in the folder to start
    std::string path = inputFolder;
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_regular_file()) {
            files.push_back(fopen(entry.path().string().c_str(), "rb"));
            char fname[128];
            _splitpath(entry.path().string().c_str(), NULL, NULL, fname, NULL);
            fileInd.push_back(atoi(fname));
            // err out
            if (files[files.size() - 1] == NULL) {
                printf("Failed to open file %s", entry.path().string().c_str());
                return;
            }
        }
    }
    // iterate over them now...
    int* fileSizes = (int*)malloc(files.size() * 4);
    int totalFileSize = 0;
    for (int i = 0; i < files.size(); ++i) {
        if (files[i] != NULL) {
            fseek(files[i], 0, SEEK_END);
            fileSizes[i] = ftell(files[i]);
            fseek(files[i], 0, SEEK_SET);
            totalFileSize += fileSizes[i];
            // this is dumb...round up to the next 0x10, then add ANOTHER 0x10
            if (fileSizes[i] % 0x10 != 0) {
                totalFileSize += 0x10 - (fileSizes[i] % 0x10);
            }
            totalFileSize += 0x10;
        }
    }
    // set up how many files we need
    int fileCount = 0;
    for (int i = 0; i < fileInd.size(); ++i) {
        fileCount = fileCount > fileInd[i] ? fileCount : fileInd[i];
    }
    ++fileCount;
    // pad to nearest 0x20
    int fileCountPadding = (fileCount * 8) + 4;
    fileCountPadding += 0x20 - (fileCountPadding % 0x20);
    // now, allocate final file size
    char* outputFile = (char*)calloc(totalFileSize + fileCountPadding, 1);
    // write file count * 2 to start
    AddToFileLE32(outputFile, 0, fileCount * 2);
    int fileOffs = fileCountPadding;
    // iterate over all the files and write their lengths and offsets
    int i2 = 0;
    for (int i = 0; i < fileCount; ++i) {
        if (i2 < fileInd.size() && fileInd[i2] == i) {
            AddToFileLE32(outputFile, 4 + i * 8, fileOffs);
            AddToFileLE32(outputFile, 8 + i * 8, fileSizes[i2]);
            fileOffs += fileSizes[i2];
            // again, round up to next 0x10 and add another 0x10
            if (fileSizes[i2] % 0x10 != 0) {
                fileOffs += 0x10 - (fileSizes[i2] % 0x10);
            }
            fileOffs += 0x10;
            ++i2;
            // skip empty files
            while (i2 < fileInd.size() && fileSizes[i2] == 0) {
                ++i2;
            }
        }
        else {
            AddToFileLE32(outputFile, 4 + i * 8, 0);
            AddToFileLE32(outputFile, 8 + i * 8, 0);
        }
    }
    // write the files themselves now
    int fileWriter = fileCountPadding;
    for (int i = 0; i < files.size(); ++i) {
        fread(outputFile + fileWriter, fileSizes[i], 1, files[i]);
        fclose(files[i]);
        fileWriter += fileSizes[i];
        if (fileSizes[i] % 0x10 != 0) {
            fileWriter += 0x10 - (fileSizes[i] % 0x10);
        }
        fileWriter += 0x10;
    }
    // and finally write the archive file itself
    FILE* archive = fopen(outputFilePath, "wb");
    if (archive == NULL) {
        printf("Failed to open output archive!");
        free(outputFile);
        free(fileSizes);
        return;
    }
    fwrite(outputFile, fileWriter, 1, archive);
    fclose(archive);
    free(outputFile);
    free(fileSizes);
}

void main(int argc, char *argv[])
{
    char* fname;
    char* oname;
    int mode = 0;
    if (argc < 4) {
        printf("Usage:\r\nBakuArchiver.exe [filemode] [input] [output]\r\nAllowed file modes:\r\n-u: unpack archive to folder\r\n-p: pack archive from folder");
        return;
    }
    if (strcmp(argv[1], "-u") == 0) {
        mode = 0;
    }
    else if (strcmp(argv[1], "-p") == 0) {
        mode = 1;
    }
    else {
        printf("Usage:\r\nBakuArchiver.exe [filemode] [input] [output]\r\nAllowed file modes:\r\n-u: unpack archive to folder\r\n-p: pack archive from folder");
        return;
    }
    fname = argv[2];
    oname = argv[3];
    // unpack file
    if (mode == 0) {
        FILE* f = fopen(fname, "rb");
        if (f == NULL) {
            printf("Failed to open file: %s!", fname);
            return;
        }
        // ensure folder ends with a leading BACKSLASH
        bool freeName = false;
        if (oname[strlen(oname) - 1] != "\\"[0]) {
            char* tmp = (char*)malloc(strlen(oname) + 2);
            sprintf(tmp, "%s%s", oname, "\\");
            oname = tmp;
            freeName = true;
        }
        UnpackArchive(f, oname);
        fclose(f);
        if (freeName) {
            free(oname);
        }
    }
    else {
        bool freeName = false;
        if (fname[strlen(fname) - 1] != "\\"[0]) {
            char* tmp = (char*)malloc(strlen(fname) + 2);
            sprintf(tmp, "%s%s", fname, "\\");
            fname = tmp;
            freeName = true;
        }
        PackArchive(fname, oname);
        if (freeName) {
            free(fname);
        }
    }
}
