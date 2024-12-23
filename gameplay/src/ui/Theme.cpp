#include "framework/Base.h"
#include "ui/Theme.h"
#include "ui/ThemeStyle.h"
#include "framework/Game.h"
#include "framework/FileSystem.h"

namespace gameplay
{

  static std::vector<Theme*> __themeCache;
  static Theme* __defaultTheme = nullptr;

  Theme::Theme() : _texture(nullptr), _spriteBatch(nullptr), _emptyImage(nullptr)
  {
  }

  Theme::~Theme()
  {
    // Destroy all the cursors, styles and , fonts.
    for (size_t i = 0, count = _styles.size(); i < count; ++i)
    {
      Style* style = _styles[i];
      SAFE_DELETE(style);
    }

    for (size_t i = 0, count = _images.size(); i < count; ++i)
    {
      ThemeImage* image = _images[i];
      SAFE_RELEASE(image);
    }

    for (size_t i = 0, count = _imageLists.size(); i < count; ++i)
    {
      ImageList* imageList = _imageLists[i];
      SAFE_RELEASE(imageList);
    }

    for (size_t i = 0, count = _skins.size(); i < count; ++i)
    {
      Skin* skin = _skins[i];
      SAFE_RELEASE(skin);
    }

    SAFE_DELETE(_spriteBatch);
    SAFE_RELEASE(_texture);

    // Remove ourself from the theme cache.
    std::vector<Theme*>::iterator itr = std::find(__themeCache.begin(), __themeCache.end(), this);
    if (itr != __themeCache.end())
    {
      __themeCache.erase(itr);
    }

    SAFE_RELEASE(_emptyImage);

    if (__defaultTheme == this)
      __defaultTheme = nullptr;
  }

  Theme* Theme::getDefault()
  {
    if (!__defaultTheme)
    {
      // Check game.config for a default theme setting
      Properties* config = Game::getInstance()->getConfig()->getNamespace("ui", true);
      if (config)
      {
        const char* defaultTheme = config->getString("theme");
        if (defaultTheme && FileSystem::fileExists(defaultTheme))
          __defaultTheme = Theme::create(defaultTheme);
      }

      if (!__defaultTheme)
      {
        // Create an empty theme so that UI's with no theme don't just crash
        GP_WARN("Creating default (empty) UI Theme.");
        __defaultTheme = new Theme();
        unsigned int color = 0x00000000;
        __defaultTheme->_texture = Texture::create(Texture::RGBA, 1, 1, (unsigned char*)&color, false);
        __defaultTheme->_emptyImage = new Theme::ThemeImage(1.0f, 1.0f, Rectangle::empty(), Vector4::zero());
        __defaultTheme->_spriteBatch = SpriteBatch::create(__defaultTheme->_texture);
        __defaultTheme->_spriteBatch->getSampler()->setFilterMode(Texture::LINEAR_MIPMAP_LINEAR, Texture::LINEAR);
        __defaultTheme->_spriteBatch->getSampler()->setWrapMode(Texture::CLAMP, Texture::CLAMP);
      }

      // TODO: Use a built-in (compiled-in) default theme resource as the final fallback so that
      // UI still works even when no theme files are present.
    }

    return __defaultTheme;
  }

  void Theme::finalize()
  {
    SAFE_RELEASE(__defaultTheme);
  }

  Theme* Theme::create(const char* url)
  {
    assert(url);

    // Search theme cache first.
    auto it = std::ranges::find_if(__themeCache, [url](Theme* t) {
      return t->_url == url;
      });

    if (it != __themeCache.end())
    {
      (*it)->addRef();
      return *it;
    }

    // Load theme properties from file path.
    Properties* properties = Properties::create(url);
    assert(properties);
    if (properties == nullptr)
    {
      return nullptr;
    }

    // Check if the Properties is valid and has a valid namespace.
    Properties* themeProperties = (strlen(properties->getNamespace()) > 0) ? properties : properties->getNextNamespace();
    assert(themeProperties);
    if (!themeProperties || !(strcmpnocase(themeProperties->getNamespace(), "theme") == 0))
    {
      SAFE_DELETE(properties);
      return nullptr;
    }

    // Create a new theme.
    // Add this theme to the cache.
    auto& theme = __themeCache.emplace_back(new Theme());

    theme->_url = url;

    // Parse the Properties object and set up the theme.
    std::string textureFile;
    themeProperties->getPath("texture", &textureFile);
    theme->_texture = Texture::create(textureFile.c_str(), true);
    assert(theme->_texture);
    theme->_spriteBatch = SpriteBatch::create(theme->_texture);
    assert(theme->_spriteBatch);
    theme->_spriteBatch->getSampler()->setFilterMode(Texture::LINEAR_MIPMAP_LINEAR, Texture::LINEAR);
    theme->_spriteBatch->getSampler()->setWrapMode(Texture::CLAMP, Texture::CLAMP);

    float tw = 1.0f / theme->_texture->getWidth();
    float th = 1.0f / theme->_texture->getHeight();

    theme->_emptyImage = new Theme::ThemeImage(tw, th, Rectangle::empty(), Vector4::zero());

    Properties* space = themeProperties->getNextNamespace();
    while (space != nullptr)
    {
      // First load all cursors, checkboxes etc. that can be referred to by styles.
      const char* spacename = space->getNamespace();

      if (strcmpnocase(spacename, "image") == 0)
      {
        theme->_images.emplace_back(ThemeImage::create(tw, th, space, Vector4::one()));
      }
      else if (strcmpnocase(spacename, "imageList") == 0)
      {
        theme->_imageLists.emplace_back(ImageList::create(tw, th, space));
      }
      else if (strcmpnocase(spacename, "skin") == 0)
      {
        Theme::Border border;
        Properties* innerSpace = space->getNextNamespace();
        if (innerSpace)
        {
          const char* innerSpacename = innerSpace->getNamespace();
          if (strcmpnocase(innerSpacename, "border") == 0)
          {
            border.top = innerSpace->getFloat("top");
            border.bottom = innerSpace->getFloat("bottom");
            border.left = innerSpace->getFloat("left");
            border.right = innerSpace->getFloat("right");
          }
        }

        Vector4 regionVector;
        space->getVector4("region", &regionVector);
        const Rectangle region(regionVector.x, regionVector.y, regionVector.z, regionVector.w);

        Vector4 color(1, 1, 1, 1);
        if (space->exists("color"))
        {
          space->getColor("color", &color);
        }

        Skin* skin = Skin::create(space->getId(), tw, th, region, border, color);
        assert(skin);
        theme->_skins.push_back(skin);
      }

      space = themeProperties->getNextNamespace();
    }

    themeProperties->rewind();
    space = themeProperties->getNextNamespace();
    while (space != nullptr)
    {
      const char* spacename = space->getNamespace();
      if (strcmpnocase(spacename, "style") == 0)
      {
        // Each style contains up to MAX_OVERLAYS overlays,
        // as well as Border and Padding namespaces.
        Theme::Margin margin;
        Theme::Padding padding;
        Theme::Style::Overlay* normal = nullptr;
        Theme::Style::Overlay* focus = nullptr;
        Theme::Style::Overlay* active = nullptr;
        Theme::Style::Overlay* disabled = nullptr;
        Theme::Style::Overlay* hover = nullptr;

        // Need to load OVERLAY_NORMAL first so that the other overlays can inherit from it.
        Properties* innerSpace = space->getNextNamespace();
        while (innerSpace != nullptr)
        {
          const char* innerSpacename = innerSpace->getNamespace();
          if (strcmpnocase(innerSpacename, "stateNormal") == 0)
          {
            Vector4 textColor(0, 0, 0, 1);
            if (innerSpace->exists("textColor"))
            {
              innerSpace->getColor("textColor", &textColor);
            }

            Font* font = nullptr;
            std::string fontPath;
            if (innerSpace->getPath("font", &fontPath))
            {
              font = Font::create(fontPath.c_str());
            }
            unsigned int fontSize = innerSpace->getInt("fontSize");
            const char* textAlignmentString = innerSpace->getString("textAlignment");
            Font::Justify textAlignment = Font::ALIGN_TOP_LEFT;
            if (textAlignmentString)
            {
              textAlignment = Font::getJustify(textAlignmentString);
            }
            bool rightToLeft = innerSpace->getBool("rightToLeft");

            float opacity = 1.0f;
            if (innerSpace->exists("opacity"))
            {
              opacity = innerSpace->getFloat("opacity");
            }

            ImageList* imageList = nullptr;
            ThemeImage* cursor = nullptr;
            Skin* skin = nullptr;
            theme->lookUpSprites(innerSpace, &imageList, &cursor, &skin);

            normal = Theme::Style::Overlay::create();
            assert(normal);
            normal->setSkin(skin);
            normal->setCursor(cursor);
            normal->setImageList(imageList);
            normal->setTextColor(textColor);
            normal->setFont(font);
            normal->setFontSize(fontSize);
            normal->setTextAlignment(textAlignment);
            normal->setTextRightToLeft(rightToLeft);
            normal->setOpacity(opacity);

            if (font)
            {
              theme->_fonts.insert(font);
              font->release();
            }

            // Done with this pass.
            break;
          }

          innerSpace = space->getNextNamespace();
        }

        // At least the OVERLAY_NORMAL is required.
        if (!normal)
        {
          normal = Theme::Style::Overlay::create();
        }

        space->rewind();
        innerSpace = space->getNextNamespace();
        while (innerSpace != nullptr)
        {
          const char* innerSpacename = innerSpace->getNamespace();
          if (strcmpnocase(innerSpacename, "margin") == 0)
          {
            margin.top = innerSpace->getFloat("top");
            margin.bottom = innerSpace->getFloat("bottom");
            margin.left = innerSpace->getFloat("left");
            margin.right = innerSpace->getFloat("right");
          }
          else if (strcmpnocase(innerSpacename, "padding") == 0)
          {
            padding.top = innerSpace->getFloat("top");
            padding.bottom = innerSpace->getFloat("bottom");
            padding.left = innerSpace->getFloat("left");
            padding.right = innerSpace->getFloat("right");
          }
          else if (strcmpnocase(innerSpacename, "stateNormal") != 0)
          {
            // Either OVERLAY_FOCUS or OVERLAY_ACTIVE.
            // If a property isn't specified, it inherits from OVERLAY_NORMAL.
            Vector4 textColor;
            if (!innerSpace->getColor("textColor", &textColor))
            {
              textColor.set(normal->getTextColor());
            }

            Font* font = nullptr;
            std::string fontPath;
            if (innerSpace->getPath("font", &fontPath))
            {
              font = Font::create(fontPath.c_str());
            }
            if (!font)
            {
              font = normal->getFont();
              if (font)
                font->addRef();
            }

            unsigned int fontSize;
            if (innerSpace->exists("fontSize"))
            {
              fontSize = innerSpace->getInt("fontSize");
            }
            else
            {
              fontSize = normal->getFontSize();
            }

            const char* textAlignmentString = innerSpace->getString("textAlignment");
            Font::Justify textAlignment;
            if (textAlignmentString)
            {
              textAlignment = Font::getJustify(textAlignmentString);
            }
            else
            {
              textAlignment = normal->getTextAlignment();
            }

            bool rightToLeft;
            if (innerSpace->exists("rightToLeft"))
            {
              rightToLeft = innerSpace->getBool("rightToLeft");
            }
            else
            {
              rightToLeft = normal->getTextRightToLeft();
            }

            float opacity;
            if (innerSpace->exists("opacity"))
            {
              opacity = innerSpace->getFloat("opacity");
            }
            else
            {
              opacity = normal->getOpacity();
            }

            ImageList* imageList = nullptr;
            ThemeImage* cursor = nullptr;
            Skin* skin = nullptr;
            theme->lookUpSprites(innerSpace, &imageList, &cursor, &skin);

            if (!imageList)
            {
              imageList = normal->getImageList();
            }

            if (!cursor)
            {
              cursor = normal->getCursor();
            }

            if (!skin)
            {
              skin = normal->getSkin();
            }

            if (strcmpnocase(innerSpacename, "stateFocus") == 0)
            {
              focus = Theme::Style::Overlay::create();
              assert(focus);
              focus->setSkin(skin);
              focus->setCursor(cursor);
              focus->setImageList(imageList);
              focus->setTextColor(textColor);
              focus->setFont(font);
              focus->setFontSize(fontSize);
              focus->setTextAlignment(textAlignment);
              focus->setTextRightToLeft(rightToLeft);
              focus->setOpacity(opacity);

              if (font)
              {
                theme->_fonts.insert(font);
                font->release();
              }
            }
            else if (strcmpnocase(innerSpacename, "stateActive") == 0)
            {
              active = Theme::Style::Overlay::create();
              assert(active);
              active->setSkin(skin);
              active->setCursor(cursor);
              active->setImageList(imageList);
              active->setTextColor(textColor);
              active->setFont(font);
              active->setFontSize(fontSize);
              active->setTextAlignment(textAlignment);
              active->setTextRightToLeft(rightToLeft);
              active->setOpacity(opacity);

              if (font)
              {
                theme->_fonts.insert(font);
                font->release();
              }
            }
            else if (strcmpnocase(innerSpacename, "stateDisabled") == 0)
            {
              disabled = Theme::Style::Overlay::create();
              assert(disabled);
              disabled->setSkin(skin);
              disabled->setCursor(cursor);
              disabled->setImageList(imageList);
              disabled->setTextColor(textColor);
              disabled->setFont(font);
              disabled->setFontSize(fontSize);
              disabled->setTextAlignment(textAlignment);
              disabled->setTextRightToLeft(rightToLeft);
              disabled->setOpacity(opacity);

              if (font)
              {
                theme->_fonts.insert(font);
                font->release();
              }
            }
            else if (strcmpnocase(innerSpacename, "stateHover") == 0)
            {
              hover = Theme::Style::Overlay::create();
              assert(hover);
              hover->setSkin(skin);
              hover->setCursor(cursor);
              hover->setImageList(imageList);
              hover->setTextColor(textColor);
              hover->setFont(font);
              hover->setFontSize(fontSize);
              hover->setTextAlignment(textAlignment);
              hover->setTextRightToLeft(rightToLeft);
              hover->setOpacity(opacity);

              if (font)
              {
                theme->_fonts.insert(font);
                font->release();
              }
            }
          }

          innerSpace = space->getNextNamespace();
        }

        if (!focus)
        {
          focus = normal;
          focus->addRef();
        }

        if (!disabled)
        {
          disabled = normal;
          disabled->addRef();
        }

        // Note: The hover and active states have their overlay left nullptr if unspecified.
        // Events will still be triggered, but a control's overlay will not be changed.
        theme->_styles.emplace_back(new Theme::Style(theme, space->getId(), tw, th, margin, padding, normal, focus, active, disabled, hover));
      }

      space = themeProperties->getNextNamespace();
    }

    SAFE_DELETE(properties);

    return theme;
  }

  Theme::Style* Theme::getStyle(const char* name) const
  {
    assert(name);

    auto it = std::ranges::find_if(_styles, [name](Style* style) {
      return style && strcmpnocase(name, style->getId()) == 0;
      });

    if (it != _styles.end())
    {
      return *it;
    }

    return nullptr;
  }

  Theme::Style* Theme::getEmptyStyle()
  {
    Theme::Style* emptyStyle = getStyle("EMPTY_STYLE");

    if (!emptyStyle)
    {
      Theme::Style::Overlay* overlay = Theme::Style::Overlay::create();
      overlay->addRef();
      overlay->addRef();
      emptyStyle = new Theme::Style(const_cast<Theme*>(this), "EMPTY_STYLE", 1.0f / _texture->getWidth(), 1.0f / _texture->getHeight(),
        Theme::Margin::empty(), Theme::Border::empty(), overlay, overlay, nullptr, overlay, nullptr);

      _styles.push_back(emptyStyle);
    }

    return emptyStyle;
  }

  void Theme::setProjectionMatrix(const Matrix& matrix)
  {
    assert(_spriteBatch);
    _spriteBatch->setProjectionMatrix(matrix);
  }

  SpriteBatch* Theme::getSpriteBatch() const
  {
    return _spriteBatch;
  }

  /**************
   * Theme::UVs *
   **************/
  Theme::UVs::UVs()
    : u1(0), v1(0), u2(0), v2(0)
  {
  }

  Theme::UVs::UVs(float u1, float v1, float u2, float v2)
    : u1(u1), v1(v1), u2(u2), v2(v2)
  {
  }

  const Theme::UVs& Theme::UVs::empty()
  {
    static UVs empty(0, 0, 0, 0);
    return empty;
  }

  const Theme::UVs& Theme::UVs::full()
  {
    static UVs full(0, 1, 1, 0);
    return full;
  }

  /**********************
   * Theme::SideRegions *
   **********************/
  const Theme::SideRegions& Theme::SideRegions::empty()
  {
    static SideRegions empty;
    return empty;
  }

  /*********************
   * Theme::ThemeImage *
   *********************/
  Theme::ThemeImage::ThemeImage(float tw, float th, const Rectangle& region, const Vector4& color)
    : _region(region), _color(color)
  {
    generateUVs(tw, th, region.x, region.y, region.width, region.height, &_uvs);
  }

  Theme::ThemeImage::~ThemeImage()
  {
  }

  Theme::ThemeImage* Theme::ThemeImage::create(float tw, float th, Properties* properties, const Vector4& defaultColor)
  {
    assert(properties);

    Vector4 regionVector;
    properties->getVector4("region", &regionVector);
    const Rectangle region(regionVector.x, regionVector.y, regionVector.z, regionVector.w);

    Vector4 color;
    if (properties->exists("color"))
    {
      properties->getColor("color", &color);
    }
    else
    {
      color.set(defaultColor);
    }

    ThemeImage* image = new ThemeImage(tw, th, region, color);
    const char* id = properties->getId();
    if (id)
    {
      image->_id = id;
    }

    return image;
  }

  const char* Theme::ThemeImage::getId() const
  {
    return _id.c_str();
  }

  const Theme::UVs& Theme::ThemeImage::getUVs() const
  {
    return _uvs;
  }

  const Rectangle& Theme::ThemeImage::getRegion() const
  {
    return _region;
  }

  const Vector4& Theme::ThemeImage::getColor() const
  {
    return _color;
  }

  /********************
   * Theme::ImageList *
   ********************/
  Theme::ImageList::ImageList(const Vector4& color) : _color(color)
  {
  }

  Theme::ImageList::ImageList(const ImageList& copy)
    : _id(copy._id), _color(copy._color)
  {
    for (const auto& image : copy._images)
    {
      assert(image);
      _images.emplace_back(new ThemeImage(*image));
    }
  }

  Theme::ImageList::~ImageList()
  {
    std::vector<ThemeImage*>::const_iterator it;
    for (it = _images.begin(); it != _images.end(); ++it)
    {
      ThemeImage* image = *it;
      SAFE_RELEASE(image);
    }
  }

  Theme::ImageList* Theme::ImageList::create(float tw, float th, Properties* properties)
  {
    assert(properties);

    Vector4 color(1, 1, 1, 1);
    if (properties->exists("color"))
    {
      properties->getColor("color", &color);
    }

    ImageList* imageList = new ImageList(color);

    const char* id = properties->getId();
    if (id)
    {
      imageList->_id = id;
    }

    Properties* space = properties->getNextNamespace();
    while (space != nullptr)
    {
      auto& image = imageList->_images.emplace_back(ThemeImage::create(tw, th, space, color));

      assert(image);

      space = properties->getNextNamespace();
    }

    return imageList;
  }

  const char* Theme::ImageList::getId() const
  {
    return _id.c_str();
  }

  Theme::ThemeImage* Theme::ImageList::getImage(const char* imageId) const
  {
    assert(imageId);

    auto it = std::ranges::find_if(_images, [imageId](ThemeImage* image) {
      return image && strcmpnocase(imageId, image->getId()) == 0;
      });

    if (it != _images.end())
    {
      return *it;
    }

    return nullptr;
  }

  /***************
   * Theme::Skin *
   ***************/
  Theme::Skin* Theme::Skin::create(const char* id, float tw, float th, const Rectangle& region, const Theme::Border& border, const Vector4& color)
  {
    Skin* skin = new Skin(tw, th, region, border, color);

    if (id)
    {
      skin->_id = id;
    }

    return skin;
  }

  Theme::Skin::Skin(float tw, float th, const Rectangle& region, const Theme::Border& border, const Vector4& color)
    : _border(border), _color(color), _region(region)
  {
    setRegion(region, tw, th);
  }

  Theme::Skin::~Skin()
  {
  }

  const char* Theme::Skin::getId() const
  {
    return _id.c_str();
  }

  const Theme::Border& Theme::Skin::getBorder() const
  {
    return _border;
  }

  const Rectangle& Theme::Skin::getRegion() const
  {
    return _region;
  }

  void Theme::Skin::setRegion(const Rectangle& region, float tw, float th)
  {
    // Can calculate all measurements in advance.
    float leftEdge = region.x * tw;
    float rightEdge = (region.x + region.width) * tw;
    float leftBorder = (region.x + _border.left) * tw;
    float rightBorder = (region.x + region.width - _border.right) * tw;

    float topEdge = 1.0f - (region.y * th);
    float bottomEdge = 1.0f - ((region.y + region.height) * th);
    float topBorder = 1.0f - ((region.y + _border.top) * th);
    float bottomBorder = 1.0f - ((region.y + region.height - _border.bottom) * th);

    // There are 9 sets of UVs to set.
    _uvs[TOP_LEFT].u1 = leftEdge;
    _uvs[TOP_LEFT].v1 = topEdge;
    _uvs[TOP_LEFT].u2 = leftBorder;
    _uvs[TOP_LEFT].v2 = topBorder;

    _uvs[TOP].u1 = leftBorder;
    _uvs[TOP].v1 = topEdge;
    _uvs[TOP].u2 = rightBorder;
    _uvs[TOP].v2 = topBorder;

    _uvs[TOP_RIGHT].u1 = rightBorder;
    _uvs[TOP_RIGHT].v1 = topEdge;
    _uvs[TOP_RIGHT].u2 = rightEdge;
    _uvs[TOP_RIGHT].v2 = topBorder;

    _uvs[LEFT].u1 = leftEdge;
    _uvs[LEFT].v1 = topBorder;
    _uvs[LEFT].u2 = leftBorder;
    _uvs[LEFT].v2 = bottomBorder;

    _uvs[CENTER].u1 = leftBorder;
    _uvs[CENTER].v1 = topBorder;
    _uvs[CENTER].u2 = rightBorder;
    _uvs[CENTER].v2 = bottomBorder;

    _uvs[RIGHT].u1 = rightBorder;
    _uvs[RIGHT].v1 = topBorder;
    _uvs[RIGHT].u2 = rightEdge;
    _uvs[RIGHT].v2 = bottomBorder;

    _uvs[BOTTOM_LEFT].u1 = leftEdge;
    _uvs[BOTTOM_LEFT].v1 = bottomBorder;
    _uvs[BOTTOM_LEFT].u2 = leftBorder;
    _uvs[BOTTOM_LEFT].v2 = bottomEdge;

    _uvs[BOTTOM].u1 = leftBorder;
    _uvs[BOTTOM].v1 = bottomBorder;
    _uvs[BOTTOM].u2 = rightBorder;
    _uvs[BOTTOM].v2 = bottomEdge;

    _uvs[BOTTOM_RIGHT].u1 = rightBorder;
    _uvs[BOTTOM_RIGHT].v1 = bottomBorder;
    _uvs[BOTTOM_RIGHT].u2 = rightEdge;
    _uvs[BOTTOM_RIGHT].v2 = bottomEdge;
  }

  const Theme::UVs& Theme::Skin::getUVs(SkinArea area) const
  {
    return _uvs[area];
  }

  const Vector4& Theme::Skin::getColor() const
  {
    return _color;
  }

  /**
   * Theme utility methods.
   */
  void Theme::generateUVs(float tw, float th, float x, float y, float width, float height, UVs* uvs)
  {
    assert(uvs);
    uvs->u1 = x * tw;
    uvs->u2 = (x + width) * tw;
    uvs->v1 = 1.0f - (y * th);
    uvs->v2 = 1.0f - ((y + height) * th);
  }

  void Theme::lookUpSprites(const Properties* overlaySpace, ImageList** imageList, ThemeImage** cursor, Skin** skin)
  {
    assert(overlaySpace);

    const char* imageListString = overlaySpace->getString("imageList");
    if (imageListString)
    {
      auto it = std::ranges::find_if(_imageLists, [imageListString](ImageList* imgList) {
        assert(imgList);
        assert(imgList->getId());
        return strcmpnocase(imgList->getId(), imageListString) == 0;
        });

      if (it != _imageLists.end())
      {
        *imageList = *it;
      }
    }

    const char* cursorString = overlaySpace->getString("cursor");
    if (cursorString)
    {
      auto it = std::ranges::find_if(_images, [cursorString](ThemeImage* image) {
        assert(image);
        assert(image->getId());
        return strcmpnocase(image->getId(), cursorString) == 0;
        });

      if (it != _images.end())
      {
        *cursor = *it;
      }
    }

    const char* skinString = overlaySpace->getString("skin");
    if (skinString)
    {
      auto it = std::ranges::find_if(_skins, [skinString](Skin* skin) {
        assert(skin);
        assert(skin->getId());
        return strcmpnocase(skin->getId(), skinString) == 0;
        });

      if (it != _skins.end()) {
        assert(skin);
        *skin = *it;
      }
    }
  }

}
