#include <teximp/jpeg/jpeg_importer.libjpeg_turbo.h>

#ifdef TEXIMP_ENABLE_JPEG_BACKEND_LIBJPEG_TURBO

#include <turbojpeg.h>

#include <gsl/gsl-lite.hpp>

namespace teximp::jpeg
{
FileFormat JpegLibJpegTurboImporter::fileFormat() const
{
    return FileFormat::Jpeg;
}

bool JpegLibJpegTurboImporter::checkSignature(std::istream& stream)
{
    std::array<uint8_t, 2> signature;
    stream.read((char*)signature.data(), 2);

    if(signature[0] == 0xff && signature[1] == 0xd8) { return true; }

    return false;
}

void JpegLibJpegTurboImporter::load(std::istream& stream, ITextureAllocator& textureAllocator,
                                    TextureImportOptions options)
{
    stream.seekg(0, std::ios_base::end);
    unsigned long jpegSize = (unsigned long)stream.tellg();
    stream.seekg(0, std::ios_base::beg);

    std::vector<uint8_t> imageData(jpegSize);
    stream.read((char*)imageData.data(), jpegSize);

    if(stream.fail())
    {
        setError(TextureImportError::FailedToOpenFile, "Could not read the file.");
        return;
    }

    int width;
    int height;
    int subsamp;
    int colorspace;

    tjhandle handle = tjInitDecompress();

    auto scopeExit = gsl::finally([&]() { tjDestroy(handle); });

    int result = tjDecompressHeader3(handle, imageData.data(), jpegSize, &width, &height, &subsamp, &colorspace);
    if(result != 0)
    {
        setError(TextureImportError::CouldNotReadHeader, tjGetErrorStr());
        tjDestroy(handle);
        return;
    }

    gpufmt::Format gpuFormat;
    TJPF jpegFormat;

    if(options.padRgbWithAlpha)
    {
        gpuFormat = gpufmt::Format::R8G8B8A8_UNORM;
        jpegFormat = TJPF::TJPF_RGBA;
    }
    else
    {
        gpuFormat = gpufmt::Format::R8G8B8_UNORM;
        jpegFormat = TJPF::TJPF_RGB;
    }

    cputex::TextureParams params{
        .format = gpuFormat,
        .dimension = cputex::TextureDimension::Texture2D,
        .extent = {width, height, 1},
        .arraySize = 1,
        .faces = 1,
        .mips = 1
    };

    textureAllocator.preAllocation(1);
    if(!textureAllocator.allocateTexture(params, 0))
    {
        setTextureAllocationError(params);
        return;
    }

    textureAllocator.postAllocation();

    std::span<std::byte> textureData =
        textureAllocator.accessTextureData(0, MipSurfaceKey{.arraySlice = 0, .face = 0, .mip = 0});

    result = tjDecompress2(handle, imageData.data(), jpegSize, castWritableBytes<unsigned char>(textureData).data(),
                           width, (int)gpufmt::formatInfo(gpuFormat).blockByteSize * width, height, jpegFormat, 0);
    if(result != 0)
    {
        setError(TextureImportError::Unknown, tjGetErrorStr());
        return;
    }
}
} // namespace teximp::jpeg

#endif // TEXIMP_ENABLE_JPEG_BACKEND_LIBJPEG_TURBO