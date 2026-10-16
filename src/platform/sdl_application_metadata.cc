#include "mocktail/platform/sdl_application_metadata.h"

#include <SDL3/SDL.h>

#include <string>
#include <cstdlib>
#include <utility>

namespace mocktail {
namespace platform {
namespace {

Status MetadataError(const char* operation) {
  std::string message = operation;
  message += " failed: ";
  const char* error = SDL_GetError();
  message += error != nullptr && error[0] != '\0' ? error : "unknown SDL error";
  return Status::Error(StatusCode::kPlatformError, std::move(message));
}

}  // namespace

Status ConfigureSdlApplicationMetadata() {
  const char* name=std::getenv("MOCKTAIL_VR_APPLICATION_NAME");
  if(!name || !name[0])name=kMocktailApplicationName;
  if (!SDL_SetAppMetadata(name, MOCKTAIL_PROJECT_VERSION,
                          kMocktailApplicationIdentifier)) {
    return MetadataError("SDL_SetAppMetadata");
  }
  if (!SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "game")) {
    return MetadataError("SDL_SetAppMetadataProperty(type)");
  }
  return Status::Ok();
}

}  // namespace platform
}  // namespace mocktail
