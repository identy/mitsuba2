#include <mitsuba/core/bitmap.h>
#include <mitsuba/core/filesystem.h>
#include <mitsuba/core/fstream.h>
#include <mitsuba/core/spectrum.h>
#include <mitsuba/core/string.h>
#include <mitsuba/render/film.h>
#include <mitsuba/render/fwd.h>
#include <mitsuba/render/imageblock.h>

NAMESPACE_BEGIN(mitsuba)

template <typename Float, typename Spectrum>
class HDRFilm final : public Film<Float, Spectrum> {
public:
    MTS_DECLARE_CLASS_VARIANT(HDRFilm, Film)
    MTS_IMPORT_BASE(Film, m_size, m_crop_size, m_crop_offset, m_high_quality_edges, m_filter)
    MTS_IMPORT_TYPES(ImageBlock)

    HDRFilm(const Properties &props) : Base(props) {
        std::string file_format = string::to_lower(
            props.string("file_format", "openexr"));
        std::string pixel_format = string::to_lower(
            props.string("pixel_format", "rgba"));
        std::string component_format = string::to_lower(
            props.string("component_format", "float16"));

        m_dest_file = props.string("filename", "");

        if (file_format == "openexr" || file_format == "exr")
            m_file_format = Bitmap::FileFormat::OpenEXR;
        else if (file_format == "rgbe")
            m_file_format = Bitmap::FileFormat::RGBE;
        else if (file_format == "pfm")
            m_file_format = Bitmap::FileFormat::PFM;
        else {
            Throw("The \"file_format\" parameter must either be "
                  "equal to \"openexr\", \"pfm\", or \"rgbe\","
                  " found %s instead.", file_format);
        }

        if (pixel_format == "luminance" || is_monochromatic_v<Spectrum>) {
            m_pixel_format = Bitmap::PixelFormat::Y;
            if (pixel_format != "luminance")
                Log(Warn,
                    "Monochrome mode enabled, setting film output pixel format "
                    "to 'luminance' (was %s).",
                    pixel_format);
        } else if (pixel_format == "luminance_alpha")
            m_pixel_format = Bitmap::PixelFormat::YA;
        else if (pixel_format == "rgb")
            m_pixel_format = Bitmap::PixelFormat::RGB;
        else if (pixel_format == "rgba")
            m_pixel_format = Bitmap::PixelFormat::RGBA;
        else if (pixel_format == "xyz")
            m_pixel_format = Bitmap::PixelFormat::XYZ;
        else if (pixel_format == "xyza")
            m_pixel_format = Bitmap::PixelFormat::XYZA;
        else {
            Throw("The \"pixel_format\" parameter must either be equal to "
                  "\"luminance\", \"luminance_alpha\", \"rgb\", \"rgba\", "
                  " \"xyz\", \"xyza\". Found %s.",
                  pixel_format);
        }

        if (component_format == "float16")
            m_component_format = Struct::Type::Float16;
        else if (component_format == "float32")
            m_component_format = Struct::Type::Float32;
        else if (component_format == "uint32")
            m_component_format = Struct::Type::UInt32;
        else {
            Throw("The \"component_format\" parameter must either be "
                  "equal to \"float16\", \"float32\", or \"uint32\"."
                  " Found %s instead.", component_format);
        }

        if (m_file_format == Bitmap::FileFormat::RGBE) {
            if (m_pixel_format != Bitmap::PixelFormat::RGB) {
                Log(Warn, "The RGBE format only supports pixel_format=\"rgb\"."
                           " Overriding..");
                m_pixel_format = Bitmap::PixelFormat::RGB;
            }
            if (m_component_format != Struct::Type::Float32) {
                Log(Warn, "The RGBE format only supports "
                           "component_format=\"float32\". Overriding..");
                m_component_format = Struct::Type::Float32;
            }
        } else if (m_file_format == Bitmap::FileFormat::PFM) {
            // PFM output; override pixel & component format if necessary
            if (m_pixel_format != Bitmap::PixelFormat::RGB && m_pixel_format != Bitmap::PixelFormat::Y) {
                Log(Warn, "The PFM format only supports pixel_format=\"rgb\""
                           " or \"luminance\". Overriding (setting to \"rgb\")..");
                m_pixel_format = Bitmap::PixelFormat::RGB;
            }
            if (m_component_format != Struct::Type::Float32) {
                Log(Warn, "The PFM format only supports"
                           " component_format=\"float32\". Overriding..");
                m_component_format = Struct::Type::Float32;
            }
        }

        const std::vector<std::string> &keys = props.property_names();
        for (size_t i = 0; i < keys.size(); ++i) {
            std::string key = string::to_lower(keys[i]);
            key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());

            if ((string::starts_with(key, "metadata['") && string::ends_with(key, "']")) ||
                (string::starts_with(key, "label[")     && string::ends_with(key, "]")))
                props.mark_queried(keys[i]);
        }
    }

    void set_destination_file(const fs::path &dest_file) override {
        m_dest_file = dest_file;
    }

    void prepare(const std::vector<std::string> &channels) override {
        std::vector<std::string> channels_sorted = channels;
        channels_sorted.push_back("R");
        channels_sorted.push_back("G");
        channels_sorted.push_back("B");
        std::sort(channels_sorted.begin(), channels_sorted.end());
        for (size_t i = 1; i < channels.size(); ++i) {
            if (channels[i] == channels[i - 1])
                Throw("Film::prepare(): duplicate channel name \"%s\"", channels[i]);
        }

        m_storage = new ImageBlock(m_crop_size, channels.size());
        m_storage->set_offset(m_crop_offset);
        m_storage->clear();
        m_channels = channels;
    }

    void put(const ImageBlock *block) override {
        Assert(m_storage != nullptr);
        m_storage->put(block);
    }

    bool develop(const ScalarPoint2i  &source_offset,
                 const ScalarVector2i &size,
                 const ScalarPoint2i  &target_offset,
                 Bitmap *target) const override {
        Assert(m_storage != nullptr);
        (void) source_offset;
        (void) size;
        (void) target_offset;
        (void) target;

#if 0
        const Bitmap *source = m_storage->bitmap();

        StructConverter cvt(source->struct_(), target->struct_());

        size_t source_bpp = source->bytes_per_pixel();
        size_t target_bpp = target->bytes_per_pixel();

        const uint8_t *source_data = source->uint8_data()
            + (source_offset.x() + source_offset.y() * source->width()) * source_bpp;
        uint8_t *target_data = target->uint8_data()
            + (target_offset.x() + target_offset.y() * target->width()) * target_bpp;

        if (size.x() == m_crop_size.x() && target->width() == m_storage->width()) {
            // Develop a connected part of the underlying buffer
            cvt.convert(size.x() * size.y(), source_data, target_data);
        } else {
            // Develop a rectangular subregion
            for (int i = 0; i < size.y(); ++i) {
                cvt.convert(size.x(), source_data, target_data);
                source_data += source->width() * source_bpp;
                target_data += target->width() * target_bpp;
            }
        }
#endif
        return true;
    }

    Bitmap *bitmap() override {
        if constexpr (is_cuda_array_v<Float>) {
            cuda_eval();
            cuda_sync();
        }

        return new Bitmap(m_channels.size() != 5 ? Bitmap::PixelFormat::MultiChannel
                                                 : Bitmap::PixelFormat::XYZAW,
                          struct_type_v<ScalarFloat>, m_storage->size(), m_storage->channel_count(),
                          (uint8_t *) m_storage->data().managed().data());
     };

    void develop() override {
        if (m_dest_file.empty())
            Throw("Destination file not specified, cannot develop.");

        fs::path filename = m_dest_file;
        std::string proper_extension;
        if (m_file_format == Bitmap::FileFormat::OpenEXR)
            proper_extension = ".exr";
        else if (m_file_format == Bitmap::FileFormat::RGBE)
            proper_extension = ".rgbe";
        else
            proper_extension = ".pfm";

        std::string extension = string::to_lower(filename.extension().string());
        if (extension != proper_extension)
            filename.replace_extension(proper_extension);

        Log(Info, "\U00002714  Developing \"%s\" ..", filename.string());

        bool has_aovs = m_channels.size() != 5;

        ref<Bitmap> source = bitmap();

        ref<Bitmap> target = new Bitmap(
            has_aovs ? Bitmap::PixelFormat::MultiChannel : m_pixel_format,
            m_component_format, m_storage->size(),
            has_aovs ? (m_storage->channel_count() - 1): 0);

        if (has_aovs) {
            for (size_t i = 0, j = 0; i < m_channels.size(); ++i, ++j) {
                Struct::Field &source_field = source->struct_()->operator[](i),
                              &dest_field   = target->struct_()->operator[](j);

                switch (i) {
                    case 0:
                        dest_field.name = "R";
                        dest_field.blend = {
                            {  3.240479f, "X" },
                            { -1.537150f, "Y" },
                            { -0.498535f, "Z" }
                        };
                        break;

                    case 1:
                        dest_field.name = "G";
                        dest_field.blend = {
                            { -0.969256, "X" },
                            {  1.875991, "Y" },
                            {  0.041556, "Z" }
                        };
                        break;

                    case 2:
                        dest_field.name = "B";
                        dest_field.blend = {
                            {  0.055648, "X" },
                            { -0.204043, "Y" },
                            {  1.057311, "Z" }
                        };
                        break;

                    case 4:
                        source_field.flags |= +Struct::Flags::Weight;
                        j--;
                        break;

                    default:
                        dest_field.name = m_channels[i];
                        break;
                }

                source_field.name = m_channels[i];
            }
        }

        source->convert(target);
        target->write(filename, m_file_format);
    }

    bool destination_exists(const fs::path &base_name) const override {
        std::string proper_extension;
        if (m_file_format == Bitmap::FileFormat::OpenEXR)
            proper_extension = ".exr";
        else if (m_file_format == Bitmap::FileFormat::RGBE)
            proper_extension = ".rgbe";
        else
            proper_extension = ".pfm";

        fs::path filename = base_name;

        std::string extension = string::to_lower(filename.extension().string());
        if (extension != proper_extension)
            filename.replace_extension(proper_extension);

        return fs::exists(filename);
    }

    std::string to_string() const override {
        std::ostringstream oss;
        oss << "HDRFilm[" << std::endl
            << "  size = " << m_size        << "," << std::endl
            << "  crop_size = " << m_crop_size   << "," << std::endl
            << "  crop_offset = " << m_crop_offset << "," << std::endl
            << "  high_quality_edges = " << m_high_quality_edges << "," << std::endl
            << "  filter = " << m_filter << "," << std::endl
            << "  file_format = " << m_file_format << "," << std::endl
            << "  pixel_format = " << m_pixel_format << "," << std::endl
            << "  component_format = " << m_component_format << "," << std::endl
            << "  dest_file = \"" << m_dest_file << "\"" << std::endl
            << "]";
        return oss.str();
    }

protected:
    Bitmap::FileFormat m_file_format;
    Bitmap::PixelFormat m_pixel_format;
    Struct::Type m_component_format;
    fs::path m_dest_file;
    ref<ImageBlock> m_storage;
    std::vector<std::string> m_channels;
};

MTS_EXPORT_PLUGIN(HDRFilm, "HDR Film");
NAMESPACE_END(mitsuba)
