#ifndef LENSFLARE
#define LENSFLARE
#include "CommonInclude.h"
#include "wiGraphicsDevice.h"

namespace wiLensFlare
{
	void Draw(GRAPHICSTHREAD threadID, const XMVECTOR& lightPos, const std::vector<wiGraphicsTypes::Texture2D*>& rims);

	void LoadShaders();
	void Initialize();
	void CleanUp();
};

#endif
