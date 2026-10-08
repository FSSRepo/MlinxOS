#include <iostream>
#include <cstdlib>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <input.png> <output.png> <width> <height>\n";
        return 1;
    }

    const char* inputFile = argv[1];
    const char* outputFile = argv[2];
    int targetWidth = std::atoi(argv[3]);
    int targetHeight = std::atoi(argv[4]);

    if (targetWidth <= 0 || targetHeight <= 0) {
        std::cerr << "Error: width and height must be positive.\n";
        return 1;
    }

    int width, height, channels;

    // Load image and force RGBA
    unsigned char* img = stbi_load(inputFile, &width, &height, &channels, 4);
    if (!img) {
        std::cerr << "Error loading the image\n";
        return 1;
    }
    channels = 4;

    std::cout << "Original image: " << width << "x" << height << "\n";

    unsigned char* resizedImg = new unsigned char[targetWidth * targetHeight * channels];

    if (!stbir_resize_uint8(
        img, width, height, 0,
        resizedImg, targetWidth, targetHeight, 0,
        channels)) {
        std::cerr << "Error resizing\n";
        stbi_image_free(img);
        delete[] resizedImg;
        return 1;
    }

    // Save PNG
    int stride = targetWidth * channels;
    if (!stbi_write_png(outputFile, targetWidth, targetHeight, channels, resizedImg, stride)) {
        std::cerr << "Error saving PNG\n";
    } else {
        std::cout << "PNG saved to: " << outputFile << "\n";
    }

    stbi_image_free(img);
    delete[] resizedImg;

    return 0;
}
