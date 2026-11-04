// Historical CP2 Section callback; used only by the frozen book-render oracle.
[](void* handle, const XpEpubRenderSpec* spec, XpEpubBuildPopup popup,
                                void* context) -> uint8_t {
        if (!popup) return section(handle).startBuild(renderSpec(*spec));
        return section(handle).startBuild(renderSpec(*spec), [popup, context] { popup(context); });
      }
