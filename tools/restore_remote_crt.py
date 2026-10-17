# PlatformIO build middleware for the one legacy source block that was
# deliberately disabled when the original patched Base64 library was removed.
#
# Densaugeo/base64_arduino is now wrapped by include/Base64.h and supplies the
# original EBTKS stream-encoder API, so the old alpha-screen code is valid
# again. Build a patched copy without modifying the developer's working tree.

import os

Import("env")


def restore_remote_crt(env, node):
    if node.name != "EBTKS_CRT.cpp":
        return node

    src_path = node.get_abspath()
    with open(src_path, "r", encoding="utf-8") as f:
        text = f.read()

    old_addr = (
        "  // Commented out for temporary fixed to the Base64 library use\n"
        "  // int addr = current_screen.sadAddr / 2;\n"
    )
    new_addr = (
        "  // Start of the visible 32 x 16 alpha page in the mirrored 32 x 64 buffer.\n"
        "  int addr = current_screen.sadAddr / 2;\n"
    )
    if old_addr not in text:
        raise RuntimeError(
            "Expected disabled CRT alpha start-address block was not found"
        )
    text = text.replace(old_addr, new_addr, 1)

    old_block = '''  else
  {
    Serial.printf("Graphic screen dump disabled due to incompatible patches in Base64.h\\r\\n");
    //TODO: Figure out how to use the unpatched Base64 lib for this.
    /*      
    Base64.beginStreamEncode(buff);
    for (int a = 0; a < 512; a++)
    {
      Base64.streamEncode(current_screen.vram[addr++]);
      if (addr > 2047)
      {
        addr = 0;
      }
    }
     Base64.finalStreamEncode();
    */
  }
'''

    new_block = '''  else
  {
    Base64.beginStreamEncode(buff);
    for (int a = 0; a < 512; a++)
    {
      Base64.streamEncode(current_screen.vram[addr++]);
      if (addr > 2047)
      {
        addr = 0;
      }
    }
    int encodedLen = Base64.finalStreamEncode();
    if (encodedLen < 0)
    {
      buff[0] = '\\0';
      Serial.printf("CRT alpha Base64 encode failed\\r\\n");
    }
  }
'''

    if old_block not in text:
        raise RuntimeError(
            "Expected disabled remote CRT Base64 block was not found in EBTKS_CRT.cpp"
        )
    text = text.replace(old_block, new_block, 1)

    out_dir = os.path.join(env.subst("$BUILD_DIR"), "generated")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "EBTKS_CRT.cpp")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(text)

    return env.File(out_path)


# Run for every source node and only replace EBTKS_CRT.cpp. This avoids
# depending on PlatformIO's platform-specific relative node path spelling.
env.AddBuildMiddleware(restore_remote_crt)
