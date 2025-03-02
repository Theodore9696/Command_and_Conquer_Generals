/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : WW3D                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/texture.cpp                            $*
 *                                                                                             *
 *                  $Org Author:: Steve_t                                                     $*
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 08/05/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 85                                                          $*
 *                                                                                             *
 * 06/27/02 KM Texture class abstraction																			*
 * 08/05/02 KM Texture class redesign (revisited)
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   FileListTextureClass::Load_Frame_Surface -- Load source texture                           *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "texture.h"

#include <d3d8.h>
#include <stdio.h>
#include <D3dx8core.h>
#include "dx8wrapper.h"
#include "targa.h"
#include <nstrdup.h>
#include "w3d_file.h"
#include "assetmgr.h"
#include "formconv.h"
#include "textureloader.h"
#include "missingtexture.h"
#include "ffactory.h"
#include "dx8caps.h"
#include "dx8texman.h"
#include "meshmatdesc.h"
#include "texturethumbnail.h"
#include "wwprofile.h"

//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")

const unsigned DEFAULT_INACTIVATION_TIME=20000;

/*
** Definitions of static members:
*/

static unsigned unused_texture_id;

// This throttles submissions to the background texture loading queue.
static unsigned TexturesAppliedPerFrame;
const unsigned MAX_TEXTURES_APPLIED_PER_FRAME=2;


/*!
 * KM General base constructor for texture classes
 */
TextureBaseClass::TextureBaseClass
(
	unsigned int width,
	unsigned int height,
	enum MipCountType mip_level_count,
	enum PoolType pool,
	bool rendertarget,
	bool reducible
)
:	MipLevelCount(mip_level_count),
	D3DTexture(NULL),
	Initialized(false),
   Name(""),
	FullPath(""),
	texture_id(unused_texture_id++),
	IsLightmap(false),
	IsProcedural(false),
	IsReducible(reducible),
	IsCompressionAllowed(false),
	InactivationTime(0),
	ExtendedInactivationTime(0),
	LastInactivationSyncTime(0),
	LastAccessed(0),
	Width(width),
	Height(height),
	Pool(pool),
	Dirty(false),
	TextureLoadTask(NULL),
	ThumbnailLoadTask(NULL),
	HSVShift(0.0f,0.0f,0.0f)
{
}


//**********************************************************************************************
//! Base texture class destructor
/*! KJM
*/
TextureBaseClass::~TextureBaseClass(void)
{
	delete TextureLoadTask;
	TextureLoadTask=NULL;
	delete ThumbnailLoadTask;
	ThumbnailLoadTask=NULL;

	if (D3DTexture) 
	{
		D3DTexture->Release();
		D3DTexture = NULL;
	}

	DX8TextureManagerClass::Remove(this);
}




//**********************************************************************************************
//! Invalidate old unused textures
/*! 
*/
void TextureBaseClass::Invalidate_Old_Unused_Textures(unsigned invalidation_time_override)
{
	// (gth) If thumbnails are not enabled, then we don't run this code.
	if (WW3D::Get_Thumbnail_Enabled() == false) {
		return;
	}

	// Zero the texture apply count in this function because this is called every frame...(this wasn't in E&B main branch KJM)
	TexturesAppliedPerFrame=0;

	unsigned synctime=WW3D::Get_Sync_Time();
	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager

	for (ite.First ();!ite.Is_Done();ite.Next ()) 
	{
		TextureClass* tex=ite.Peek_Value();

		// Consider invalidating if texture has been initialized and defines inactivation time
		if (tex->Initialized && tex->InactivationTime) 
		{
			unsigned age=synctime-tex->LastAccessed;

			if (invalidation_time_override) 
			{
				if (age>invalidation_time_override) 
				{
					tex->Invalidate();
					tex->LastInactivationSyncTime=synctime;
				}
			}
			else 
			{
				// Not used in the last n milliseconds?
				if (age>(tex->InactivationTime+tex->ExtendedInactivationTime)) 
				{
					tex->Invalidate();
					tex->LastInactivationSyncTime=synctime;
				}
			}
		}
	}
}





//**********************************************************************************************
//! Invalidate this texture
/*! 
*/
void TextureBaseClass::Invalidate()
{
	if (TextureLoadTask) {
		return;
	}
	if (ThumbnailLoadTask) {
		return;
	}

	// Don't invalidate procedural textures
	if (IsProcedural) {
		return;
	}

	if (D3DTexture) 
	{
		D3DTexture->Release();
		D3DTexture = NULL;
	}

	Initialized=false;

	LastAccessed=WW3D::Get_Sync_Time();
/*	was battlefield version// If the texture has already been initialised we should exit now
	if (Initialized) return;

	WWPROFILE(("TextureClass::Init()"));

	// If the texture has recently been inactivated, increase the inactivation time (this texture obviously
	// should not have been inactivated yet).

	if (InactivationTime && LastInactivationSyncTime) {
		if ((WW3D::Get_Sync_Time()-LastInactivationSyncTime)<InactivationTime) {
			ExtendedInactivationTime=3*InactivationTime;
		}
		LastInactivationSyncTime=0;
	}

	if (ThumbnailLoadTask) 
	{
		return;
	}

	// Don't invalidate procedural textures
	if (IsProcedural) 
	{
		return;
	}

	if (D3DTexture) 
	{
		D3DTexture->Release();
		D3DTexture = NULL;
	}

	Initialized=false;

	LastAccessed=WW3D::Get_Sync_Time();*/
}

//**********************************************************************************************
//! Returns a pointer to the d3d texture
/*! 
*/
IDirect3DBaseTexture8 * TextureBaseClass::Peek_D3D_Base_Texture() const 
{ 	
	LastAccessed=WW3D::Get_Sync_Time(); 
	return D3DTexture; 
}

//**********************************************************************************************
//! Set the d3d texture pointer.  Handles ref counts properly.
/*! 
*/
void TextureBaseClass::Set_D3D_Base_Texture(IDirect3DBaseTexture8* tex) 
{ 
	// (gth) Generals does stuff directly with the D3DTexture pointer so lets
	// reset the access timer whenever someon messes with this pointer.
	LastAccessed=WW3D::Get_Sync_Time();
	
	if (D3DTexture != NULL) {
		D3DTexture->Release();
	}
	D3DTexture = tex;
	if (D3DTexture != NULL) {
		D3DTexture->AddRef();
	}
}


//**********************************************************************************************
//! Load locked surface
/*! 
*/
void TextureBaseClass::Load_Locked_Surface()
{
	WWPROFILE(("TextureClass::Load_Locked_Surface()"));
	if (D3DTexture) D3DTexture->Release();
	D3DTexture=0;
	TextureLoader::Request_Thumbnail(this);
	Initialized=false;
}


//**********************************************************************************************
//! Is missing texture
/*! 
*/
bool TextureBaseClass::Is_Missing_Texture()
{
	bool flag = false;
	IDirect3DBaseTexture8 *missing_texture = MissingTexture::_Get_Missing_Texture();

	if (D3DTexture == missing_texture)
		flag = true;

	if (missing_texture)
	{
		missing_texture->Release();
	}

	return flag;
}


//**********************************************************************************************
//! Set texture name
/*! 
*/
void TextureBaseClass::Set_Texture_Name(const char * name)
{
	Name=name;
}




//**********************************************************************************************
//! Get priority
/*! 
*/
unsigned int TextureBaseClass::Get_Priority(void)
{
	if (!D3DTexture) 
	{
		WWASSERT_PRINT(0, "Get_Priority: D3DTexture is NULL!\n");
		return 0;
	}

#ifndef _XBOX
	return D3DTexture->GetPriority();
#else
	return 0;
#endif
}


//**********************************************************************************************
//! Set priority
/*! 
*/
unsigned int TextureBaseClass::Set_Priority(unsigned int priority)
{
	if (!D3DTexture) 
	{
		WWASSERT_PRINT(0, "Set_Priority: D3DTexture is NULL!\n");
		return 0;
	}

#ifndef _XBOX
	return D3DTexture->SetPriority(priority);
#else
	return 0;
#endif
}


//**********************************************************************************************
//! Get reduction mip levels
/*! 
*/
unsigned TextureBaseClass::Get_Reduction() const
{
	// don't reduce if the texture is too small already or
	// has no mip map levels
	if (MipLevelCount==MIP_LEVELS_1) return 0;
	if (Width <= 32 || Height <= 32) return 0;

	int reduction=WW3D::Get_Texture_Reduction();

	// 'large texture extra reduction' causes textures above 256x256 to be reduced one more step.
	if (WW3D::Is_Large_Texture_Extra_Reduction_Enabled() && (Width > 256 || Height > 256)) {
		reduction++;
	}
	if (MipLevelCount && reduction>MipLevelCount) {
		reduction=MipLevelCount;
	}
	return reduction;
}



//**********************************************************************************************
//! Apply NULL texture state
/*! 
*/
void TextureBaseClass::Apply_Null(unsigned int stage)
{
	// This function sets the render states for a "NULL" texture
	DX8Wrapper::Set_DX8_Texture(stage, NULL);
}

// ----------------------------------------------------------------------------
// Setting HSV_Shift value is always relative to the original texture. This function invalidates the
// texture surface and causes the texture to be reloaded. For thumbnailable textures, the hue shifting
// is done in the background loading thread.
// ----------------------------------------------------------------------------
void TextureBaseClass::Set_HSV_Shift(const Vector3 &hsv_shift)
{
	Invalidate();
	HSVShift=hsv_shift;
}

//**********************************************************************************************
//! Get total locked surface size
/*! KM
*/
int TextureBaseClass::_Get_Total_Locked_Surface_Size()
{
	int total_locked_surface_size=0;

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager
	for (ite.First ();!ite.Is_Done();ite.Next ()) 
	{
		// Get the current texture
		TextureBaseClass* tex=ite.Peek_Value();
		if (!tex->Initialized) 
		{
			total_locked_surface_size+=tex->Get_Texture_Memory_Usage();
		}
	}
	return total_locked_surface_size;
}

//**********************************************************************************************
//! Get total texture size
/*! KM
*/
int TextureBaseClass::_Get_Total_Texture_Size()
{
	int total_texture_size=0;

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager
	for (ite.First ();!ite.Is_Done();ite.Next ()) 
	{
		// Get the current texture
		TextureBaseClass* tex=ite.Peek_Value();
		total_texture_size+=tex->Get_Texture_Memory_Usage();
	}
	return total_texture_size;
}

// ----------------------------------------------------------------------------


//**********************************************************************************************
//! Get total lightmap texture size
/*!
*/
int TextureBaseClass::_Get_Total_Lightmap_Texture_Size()
{
	int total_texture_size=0;

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager
	for (ite.First ();!ite.Is_Done();ite.Next ()) 
	{
		// Get the current texture
		TextureBaseClass* tex=ite.Peek_Value();
		if (tex->Is_Lightmap()) 
		{
			total_texture_size+=tex->Get_Texture_Memory_Usage();
		}
	}
	return total_texture_size;
}


//**********************************************************************************************
//! Get total procedural texture size
/*!
*/
int TextureBaseClass::_Get_Total_Procedural_Texture_Size()
{
	int total_texture_size=0;

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager
	for (ite.First ();!ite.Is_Done();ite.Next ()) 
	{
		// Get the current texture
		TextureBaseClass* tex=ite.Peek_Value();
		if (tex->Is_Procedural()) 
		{
			total_texture_size+=tex->Get_Texture_Memory_Usage();
		}
	}
	return total_texture_size;
}

//**********************************************************************************************
//! Get total texture count
/*!
*/
int TextureBaseClass::_Get_Total_Texture_Count()
{
	int texture_count=0;

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager
	for (ite.First ();!ite.Is_Done();ite.Next ()) 
	{
		texture_count++;
	}

	return texture_count;
}

// ----------------------------------------------------------------------------


//**********************************************************************************************
//! Get total light map texture count
/*!
*/
int TextureBaseClass::_Get_Total_Lightmap_Texture_Count()
{
	int texture_count=0;

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager
	for (ite.First ();!ite.Is_Done();ite.Next ()) 
	{
		if (ite.Peek_Value()->Is_Lightmap()) 
		{
			texture_count++;
		}
	}

	return texture_count;
}

//**********************************************************************************************
//! Get total procedural texture count
/*!
*/
int TextureBaseClass::_Get_Total_Procedural_Texture_Count()
{
	int texture_count=0;

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager
	for (ite.First ();!ite.Is_Done();ite.Next ()) 
	{
		if (ite.Peek_Value()->Is_Procedural()) 
		{
			texture_count++;
		}
	}

	return texture_count;
}


//**********************************************************************************************
//! Get total locked surface count
/*!
*/
int TextureBaseClass::_Get_Total_Locked_Surface_Count()
{
	int texture_count=0;

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager
	for (ite.First ();!ite.Is_Done();ite.Next ()) 
	{
		// Get the current texture
		TextureBaseClass* tex=ite.Peek_Value();
		if (!tex->Initialized) 
		{
			texture_count++;
		}
	}

	return texture_count;
}

/*************************************************************************
**                             TextureClass
*************************************************************************/
TextureClass::TextureClass
(
	unsigned width, 
	unsigned height, 
	WW3DFormat format, 
	MipCountType mip_level_count, 
	PoolType pool,
	bool rendertarget,
	bool allow_reduction
)
:	TextureBaseClass(width, height, mip_level_count, pool, rendertarget,allow_reduction),
	Filter(mip_level_count),
	TextureFormat(format)
{
	Initialized=true;
	IsProcedural=true;
	IsReducible=false;

	switch (format) 
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default : break;
	}
		
	D3DPOOL d3dpool=(D3DPOOL)0;
	switch(pool)
	{
	case POOL_DEFAULT		: d3dpool=D3DPOOL_DEFAULT; break;
	case POOL_MANAGED		: d3dpool=D3DPOOL_MANAGED; break;
	case POOL_SYSTEMMEM	: d3dpool=D3DPOOL_SYSTEMMEM; break;
	default: WWASSERT(0);
	}

	Poke_Texture
	(
		DX8Wrapper::_Create_DX8_Texture
		(
			width, 
			height, 
			format, 
			mip_level_count,
			d3dpool,
			rendertarget
		)
	);

	if (pool==POOL_DEFAULT)
	{
		Set_Dirty();
		DX8TextureTrackerClass *track=new DX8TextureTrackerClass
		(
			width, 
			height, 
			format, 
			mip_level_count,
			this,
			rendertarget
		);
		DX8TextureManagerClass::Add(track);
	}
	LastAccessed=WW3D::Get_Sync_Time();
}



// ----------------------------------------------------------------------------
TextureClass::TextureClass
(
	const char *name,
	const char *full_path,
	MipCountType mip_level_count,
	WW3DFormat texture_format,
	bool allow_compression,
	bool allow_reduction
)
:	TextureBaseClass(0, 0, mip_level_count),
	Filter(mip_level_count),
	TextureFormat(texture_format)
{
	IsCompressionAllowed=allow_compression;
	InactivationTime=DEFAULT_INACTIVATION_TIME;		// Default inactivation time 30 seconds
	IsReducible=allow_reduction;

	switch (TextureFormat) 
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	case WW3D_FORMAT_U8V8:		// Bumpmap
	case WW3D_FORMAT_L6V5U5:	// Bumpmap
	case WW3D_FORMAT_X8L8V8U8:	// Bumpmap
		// If requesting bumpmap format that isn't available we'll just return the surface in whatever color
		// format the texture file is in. (This is illegal case, the format support should always be queried
		// before creating a bump texture!)
		if (!DX8Wrapper::Is_Initted() || !DX8Wrapper::Get_Current_Caps()->Support_Texture_Format(TextureFormat)) 
		{
			TextureFormat=WW3D_FORMAT_UNKNOWN;
		}
		// If bump format is valid, make sure compression is not allowed so that we don't even attempt to load
		// from a compressed file (quality isn't good enough for bump map). Also disable mipmapping.
		else 
		{
			IsCompressionAllowed=false;
			MipLevelCount=MIP_LEVELS_1;
			Filter.Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);
		}
		break;
	default:	break;
	}

	WWASSERT_PRINT(name && name[0], "TextureClass CTor: NULL or empty texture name\n");
	int len=strlen(name);
	for (int i=0;i<len;++i) 
	{
		if (name[i]=='+') 
		{
			IsLightmap=true;

			// Set bilinear filtering for lightmaps (they are very stretched and
			// low detail so we don't care for anisotropic or trilinear filtering...)
			Filter.Set_Min_Filter(TextureFilterClass::FILTER_TYPE_FAST);
			Filter.Set_Mag_Filter(TextureFilterClass::FILTER_TYPE_FAST);
			if (mip_level_count!=MIP_LEVELS_1) Filter.Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_FAST);
			break;
		}
	}
	Set_Texture_Name(name);
	Set_Full_Path(full_path);
	WWASSERT(name[0]!='\0');
	if (!WW3D::Is_Texturing_Enabled()) 
	{
		Initialized=true;
		Poke_Texture(NULL);
	}

	// Find original size from the thumbnail (but don't create thumbnail texture yet!)
	ThumbnailClass* thumb=ThumbnailManagerClass::Peek_Thumbnail_Instance_From_Any_Manager(Get_Full_Path());
	if (thumb) 
	{
		Width=thumb->Get_Original_Texture_Width();
		Height=thumb->Get_Original_Texture_Height();
 		if (MipLevelCount!=MIP_LEVELS_1) {
 			MipLevelCount=(MipCountType)thumb->Get_Original_Texture_Mip_Level_Count();
 		}
	}

	LastAccessed=WW3D::Get_Sync_Time();

	// If the thumbnails are not enabled, init the texture at this point to avoid stalling when the
	// mesh is rendered.
	if (!WW3D::Get_Thumbnail_Enabled()) 
	{
		if (TextureLoader::Is_DX8_Thread()) 
		{
			Init();
		}
	}
}

// ----------------------------------------------------------------------------
TextureClass::TextureClass
(
	SurfaceClass *surface, 
	MipCountType mip_level_count
)
:  TextureBaseClass(0,0,mip_level_count),
	Filter(mip_level_count),
	TextureFormat(surface->Get_Surface_Format())
{
	IsProcedural=true;
	Initialized=true;
	IsReducible=false;

	SurfaceClass::SurfaceDescription sd;
	surface->Get_Description(sd);
	Width=sd.Width;
	Height=sd.Height;
	switch (sd.Format) 
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default: break;
	}

	Poke_Texture
	(
		DX8Wrapper::_Create_DX8_Texture
		(
			surface->Peek_D3D_Surface(), 
			mip_level_count
		)
	);
	LastAccessed=WW3D::Get_Sync_Time();
}

// ----------------------------------------------------------------------------
TextureClass::TextureClass(IDirect3DBaseTexture8* d3d_texture)
:	TextureBaseClass
	(
		0,
		0,
		((MipCountType)d3d_texture->GetLevelCount())
	),
	Filter((MipCountType)d3d_texture->GetLevelCount())
{
	Initialized=true;
	IsProcedural=true;
	IsReducible=false;
	
	Set_D3D_Base_Texture(d3d_texture);
	IDirect3DSurface8* surface;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(0,&surface));
	D3DSURFACE_DESC d3d_desc;
	::ZeroMemory(&d3d_desc, sizeof(D3DSURFACE_DESC));
	DX8_ErrorCode(surface->GetDesc(&d3d_desc));
	Width=d3d_desc.Width;
	Height=d3d_desc.Height;
	TextureFormat=D3DFormat_To_WW3DFormat(d3d_desc.Format);
	switch (TextureFormat) 
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default: break;
	}

	LastAccessed=WW3D::Get_Sync_Time();
}

//**********************************************************************************************
//! Initialise the texture
/*! 
*/
void TextureClass::Init()
{
	// If the texture has already been initialised we should exit now
	if (Initialized) return;

	WWPROFILE("TextureClass::Init");

	// If the texture has recently been inactivated, increase the inactivation time (this texture obviously
	// should not have been inactivated yet).
	if (InactivationTime && LastInactivationSyncTime) 
	{
		if ((WW3D::Get_Sync_Time()-LastInactivationSyncTime)<InactivationTime) 
		{
			ExtendedInactivationTime=3*InactivationTime;
		}
		LastInactivationSyncTime=0;
	}


	if (!Peek_D3D_Base_Texture()) 
	{
		if (!WW3D::Get_Thumbnail_Enabled() || MipLevelCount==MIP_LEVELS_1) 
		{
//		if (MipLevelCount==MIP_LEVELS_1) {
			TextureLoader::Request_Foreground_Loading(this);
		}
		else 
		{
			WW3DFormat format=TextureFormat;
			Load_Locked_Surface();
			TextureFormat=format;
		}
	}

	if (!Initialized) 
	{
		TextureLoader::Request_Background_Loading(this);
	}

	LastAccessed=WW3D::Get_Sync_Time();
}

//**********************************************************************************************
//! Apply new surface to texture
/*! 
*/
void TextureClass::Apply_New_Surface
(
	IDirect3DBaseTexture8* d3d_texture,
	bool initialized,
	bool disable_auto_invalidation
)
{
	IDirect3DBaseTexture8* d3d_tex=Peek_D3D_Base_Texture();

	if (d3d_tex) d3d_tex->Release();

	Poke_Texture(d3d_texture);//TextureLoadTask->Peek_D3D_Texture();
	d3d_texture->AddRef();

	if (initialized) Initialized=true;
	if (disable_auto_invalidation) InactivationTime = 0;

	WWASSERT(d3d_texture);
	IDirect3DSurface8* surface;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(0,&surface));
	D3DSURFACE_DESC d3d_desc;
	::ZeroMemory(&d3d_desc, sizeof(D3DSURFACE_DESC));
	DX8_ErrorCode(surface->GetDesc(&d3d_desc));
	if (initialized) 
	{
		TextureFormat=D3DFormat_To_WW3DFormat(d3d_desc.Format);
		Width=d3d_desc.Width;
		Height=d3d_desc.Height;
	}
	surface->Release();

}


//**********************************************************************************************
//! Apply texture states
/*! 
*/
void TextureClass::Apply(unsigned int stage)
{
	// Initialization needs to be done when texture is used if it hasn't been done before.
	// XBOX always initializes textures at creation time.
	if (!Initialized) 
	{
		Init();

		/* was in battlefield// Non-thumbnailed textures are always initialized when used
		if (MipLevelCount==MIP_LEVELS_1) 
		{
		}
		// Thumbnailed textures have delayed initialization and a background loading system
		else 
		{
			// Limit the number of texture initializations per frame to reduce stuttering
			if (TexturesAppliedPerFrame<MAX_TEXTURES_APPLIED_PER_FRAME) 
			{
				TexturesAppliedPerFrame++;
				Init();
			}
			else 
			{
				// If texture can't be initialized in this frame, at least make sure we have the thumbnail.
				if (!Peek_Texture()) 
				{
					WW3DFormat format=TextureFormat;
					Load_Locked_Surface();
					TextureFormat=format;
				}
			}
		}*/
	}
	LastAccessed=WW3D::Get_Sync_Time();

	DX8_RECORD_TEXTURE(this);

	// Set texture itself
	if (WW3D::Is_Texturing_Enabled()) 
	{
		DX8Wrapper::Set_DX8_Texture(stage, Peek_D3D_Base_Texture());
	}
	else 
	{
		DX8Wrapper::Set_DX8_Texture(stage, NULL);
	}

	Filter.Apply(stage);
}

//**********************************************************************************************
//! Get surface from mip level
/*! 
*/
SurfaceClass *TextureClass::Get_Surface_Level(unsigned int level)
{
	if (!Peek_D3D_Texture()) 
	{
		WWASSERT_PRINT(0, "Get_Surface_Level: D3DTexture is NULL!\n");
		return 0;
	}

	IDirect3DSurface8 *d3d_surface = NULL;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(level, &d3d_surface));
	SurfaceClass *surface = new SurfaceClass(d3d_surface);
	d3d_surface->Release();

	return surface;
}

//**********************************************************************************************
//! Get surface description for a mip level
/*!
*/
void TextureClass::Get_Level_Description( SurfaceClass::SurfaceDescription & desc, unsigned int level )
{
	SurfaceClass * surf = Get_Surface_Level(level);
	if (surf != NULL) {
		surf->Get_Description(desc);
	}
	REF_PTR_RELEASE(surf);
}

//**********************************************************************************************
//! Get D3D surface from mip level
/*! 
*/
IDirect3DSurface8 *TextureClass::Get_D3D_Surface_Level(unsigned int level)
{
	if (!Peek_D3D_Texture()) 
	{
		WWASSERT_PRINT(0, "Get_D3D_Surface_Level: D3DTexture is NULL!\n");
		return 0;
	}

	IDirect3DSurface8 *d3d_surface = NULL;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(level, &d3d_surface));
	return d3d_surface;
}

//**********************************************************************************************
//! Get texture memory usage
/*! 
*/
unsigned TextureClass::Get_Texture_Memory_Usage() const
{
	int size=0;
	if (!Peek_D3D_Texture()) return 0;
	for (unsigned i=0;i<Peek_D3D_Texture()->GetLevelCount();++i) 
	{
		D3DSURFACE_DESC desc;
		DX8_ErrorCode(Peek_D3D_Texture()->GetLevelDesc(i,&desc));
		size+=desc.Size;
	}
	return size;
}


// Utility functions
TextureClass* Load_Texture(ChunkLoadClass & cload)
{
	// Assume failure
	TextureClass *newtex = NULL;

	char name[256];
	if (cload.Open_Chunk () && (cload.Cur_Chunk_ID () == W3D_CHUNK_TEXTURE)) 
	{

		W3dTextureInfoStruct texinfo;
		bool hastexinfo = false;

		/*
		** Read in the texture filename, and a possible texture info structure.
		*/
		while (cload.Open_Chunk()) {
			switch (cload.Cur_Chunk_ID()) {
				case W3D_CHUNK_TEXTURE_NAME:
					cload.Read(&name,cload.Cur_Chunk_Length());
					break;

				case W3D_CHUNK_TEXTURE_INFO:
					cload.Read(&texinfo,sizeof(W3dTextureInfoStruct));
					hastexinfo = true;
					break;
			};
			cload.Close_Chunk();
		}
		cload.Close_Chunk();

		/*
		** Get the texture from the asset manager
		*/
		if (hastexinfo) 
		{

			MipCountType mipcount;

			bool no_lod = ((texinfo.Attributes & W3DTEXTURE_NO_LOD) == W3DTEXTURE_NO_LOD);

			if (no_lod) 
			{
				mipcount = MIP_LEVELS_1;
			} 
			else 
			{
				switch (texinfo.Attributes & W3DTEXTURE_MIP_LEVELS_MASK) {

					case W3DTEXTURE_MIP_LEVELS_ALL:
						mipcount = MIP_LEVELS_ALL;
						break;

					case W3DTEXTURE_MIP_LEVELS_2:
						mipcount = MIP_LEVELS_2;
						break;

					case W3DTEXTURE_MIP_LEVELS_3:
						mipcount = MIP_LEVELS_3;
						break;

					case W3DTEXTURE_MIP_LEVELS_4:
						mipcount = MIP_LEVELS_4;
						break;

					default:
						WWASSERT (false);
						mipcount = MIP_LEVELS_ALL;
						break;
				}
			}

			WW3DFormat format=WW3D_FORMAT_UNKNOWN;

			switch (texinfo.Attributes & W3DTEXTURE_TYPE_MASK) 
			{

				case W3DTEXTURE_TYPE_COLORMAP:
					// Do nothing.
					break;

				case W3DTEXTURE_TYPE_BUMPMAP:
				{
					if (DX8Wrapper::Is_Initted() && DX8Wrapper::Get_Current_Caps()->Support_Bump_Envmap()) 
					{
						// No mipmaps to bumpmap for now
						mipcount=MIP_LEVELS_1;

						if (DX8Wrapper::Get_Current_Caps()->Support_Texture_Format(WW3D_FORMAT_U8V8)) format=WW3D_FORMAT_U8V8;
						else if (DX8Wrapper::Get_Current_Caps()->Support_Texture_Format(WW3D_FORMAT_X8L8V8U8)) format=WW3D_FORMAT_X8L8V8U8;
						else if (DX8Wrapper::Get_Current_Caps()->Support_Texture_Format(WW3D_FORMAT_L6V5U5)) format=WW3D_FORMAT_L6V5U5;
					}
					break;
				}

				default:
					WWASSERT (false);
					break;
			}

			newtex = WW3DAssetManager::Get_Instance()->Get_Texture (name, mipcount, format);

			if (no_lod) 
			{
				newtex->Get_Filter().Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);
			}
			bool u_clamp = ((texinfo.Attributes & W3DTEXTURE_CLAMP_U) != 0);
			newtex->Get_Filter().Set_U_Addr_Mode(u_clamp ? TextureFilterClass::TEXTURE_ADDRESS_CLAMP : TextureFilterClass::TEXTURE_ADDRESS_REPEAT);
			bool v_clamp = ((texinfo.Attributes & W3DTEXTURE_CLAMP_V) != 0);
			newtex->Get_Filter().Set_V_Addr_Mode(v_clamp ? TextureFilterClass::TEXTURE_ADDRESS_CLAMP : TextureFilterClass::TEXTURE_ADDRESS_REPEAT);

		} else 
		{
			newtex = WW3DAssetManager::Get_Instance()->Get_Texture(name);
		}

		WWASSERT(newtex);
	}

	// Return a pointer to the new texture
	return newtex;
}

// Utility function used by Save_Texture
void setup_texture_attributes(TextureClass * tex, W3dTextureInfoStruct * texinfo)
{
	texinfo->Attributes = 0;

	if (tex->Get_Filter().Get_Mip_Mapping() == TextureFilterClass::FILTER_TYPE_NONE) texinfo->Attributes |= W3DTEXTURE_NO_LOD;
	if (tex->Get_Filter().Get_U_Addr_Mode() == TextureFilterClass::TEXTURE_ADDRESS_CLAMP) texinfo->Attributes |= W3DTEXTURE_CLAMP_U;
	if (tex->Get_Filter().Get_V_Addr_Mode() == TextureFilterClass::TEXTURE_ADDRESS_CLAMP) texinfo->Attributes |= W3DTEXTURE_CLAMP_V;
}


void Save_Texture(TextureClass * texture,ChunkSaveClass & csave)
{
	const char * filename;
	W3dTextureInfoStruct texinfo;
	memset(&texinfo,0,sizeof(texinfo));

	filename = texture->Get_Full_Path();

	setup_texture_attributes(texture, &texinfo);

	csave.Begin_Chunk(W3D_CHUNK_TEXTURE_NAME);
	csave.Write(filename,strlen(filename)+1);
	csave.End_Chunk();

	if ((texinfo.Attributes != 0) || (texinfo.AnimType != 0) || (texinfo.FrameCount != 0)) {
		csave.Begin_Chunk(W3D_CHUNK_TEXTURE_INFO);
		csave.Write(&texinfo, sizeof(texinfo));
		csave.End_Chunk();
	}
}


/*!
 *	KJM depth stencil texture constructor
 */
ZTextureClass::ZTextureClass
(
	unsigned width, 
	unsigned height, 
	WW3DZFormat zformat, 
	MipCountType mip_level_count, 
	PoolType pool
)
:	TextureBaseClass(width,height, mip_level_count, pool),
	DepthStencilTextureFormat(zformat)
{
	D3DPOOL d3dpool=(D3DPOOL)0;
	switch (pool)
	{
	case POOL_DEFAULT: d3dpool=D3DPOOL_DEFAULT; break;
	case POOL_MANAGED: d3dpool=D3DPOOL_MANAGED; break;
	case POOL_SYSTEMMEM: d3dpool=D3DPOOL_SYSTEMMEM;	break;
	default:	WWASSERT(0);
	}

	Poke_Texture
	(
		DX8Wrapper::_Create_DX8_ZTexture
		(
			width,
			height, 
			zformat, 
			mip_level_count,
			d3dpool
		)
	);

	if (pool==POOL_DEFAULT)
	{
		Set_Dirty();
		DX8ZTextureTrackerClass *track=new DX8ZTextureTrackerClass
		(
			width, 
			height, 
			zformat, 
			mip_level_count,
			this
		);
		DX8TextureManagerClass::Add(track);
	}
	Initialized=true;
	IsProcedural=true;
	IsReducible=false;

	LastAccessed=WW3D::Get_Sync_Time();
}


//**********************************************************************************************
//! Apply depth stencil texture
/*! KM
*/
void ZTextureClass::Apply(unsigned int stage)
{
	DX8Wrapper::Set_DX8_Texture(stage, Peek_D3D_Base_Texture());
}

//**********************************************************************************************
//! Apply new surface to texture
/*! KM
*/
void ZTextureClass::Apply_New_Surface
(
	IDirect3DBaseTexture8* d3d_texture,
	bool initialized,
	bool disable_auto_invalidation
)
{
	IDirect3DBaseTexture8* d3d_tex=Peek_D3D_Base_Texture();

	if (d3d_tex) d3d_tex->Release();

	Poke_Texture(d3d_texture);//TextureLoadTask->Peek_D3D_Texture();
	d3d_texture->AddRef();

	if (initialized) Initialized=true;
	if (disable_auto_invalidation) InactivationTime = 0;

	WWASSERT(Peek_D3D_Texture());
	IDirect3DSurface8* surface;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(0,&surface));
	D3DSURFACE_DESC d3d_desc;
	::ZeroMemory(&d3d_desc, sizeof(D3DSURFACE_DESC));
	DX8_ErrorCode(surface->GetDesc(&d3d_desc));
	if (initialized) 
	{
		DepthStencilTextureFormat=D3DFormat_To_WW3DZFormat(d3d_desc.Format);
		Width=d3d_desc.Width;
		Height=d3d_desc.Height;
	}
	surface->Release();
}

//**********************************************************************************************
//! Get D3D surface from mip level
/*! 
*/
IDirect3DSurface8* ZTextureClass::Get_D3D_Surface_Level(unsigned int level)
{
	if (!Peek_D3D_Texture()) 
	{
		WWASSERT_PRINT(0, "Get_D3D_Surface_Level: D3DTexture is NULL!\n");
		return 0;
	}

	IDirect3DSurface8 *d3d_surface = NULL;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(level, &d3d_surface));
	return d3d_surface;
}

//**********************************************************************************************
//! Get texture memory usage
/*! 
*/
unsigned ZTextureClass::Get_Texture_Memory_Usage() const
{
	int size=0;
	if (!Peek_D3D_Texture()) return 0;
	for (unsigned i=0;i<Peek_D3D_Texture()->GetLevelCount();++i) 
	{
		D3DSURFACE_DESC desc;
		DX8_ErrorCode(Peek_D3D_Texture()->GetLevelDesc(i,&desc));
		size+=desc.Size;
	}
	return size;
}



/*************************************************************************
**                             CubeTextureClass
*************************************************************************/
CubeTextureClass::CubeTextureClass
(
	unsigned width, 
	unsigned height, 
	WW3DFormat format, 
	MipCountType mip_level_count, 
	PoolType pool,
	bool rendertarget,
	bool allow_reduction
)
: TextureClass(width, height, format, mip_level_count, pool, rendertarget)
{
	Initialized=true;
	IsProcedural=true;
	IsReducible=false;

	switch (format) 
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default : break;
	}
		
	D3DPOOL d3dpool=(D3DPOOL)0;
	switch(pool)
	{
	case POOL_DEFAULT		: d3dpool=D3DPOOL_DEFAULT; break;
	case POOL_MANAGED		: d3dpool=D3DPOOL_MANAGED; break;
	case POOL_SYSTEMMEM	: d3dpool=D3DPOOL_SYSTEMMEM; break;
	default: WWASSERT(0);
	}

	Poke_Texture
	(
		DX8Wrapper::_Create_DX8_Cube_Texture
		(
			width, 
			height, 
			format, 
			mip_level_count,
			d3dpool,
			rendertarget
		)
	);

	if (pool==POOL_DEFAULT)
	{
		Set_Dirty();
		DX8TextureTrackerClass *track=new DX8TextureTrackerClass
		(
			width, 
			height, 
			format, 
			mip_level_count,
			this,
			rendertarget
		);
		DX8TextureManagerClass::Add(track);
	}
	LastAccessed=WW3D::Get_Sync_Time();
}



// ----------------------------------------------------------------------------
CubeTextureClass::CubeTextureClass
(
	const char *name,
	const char *full_path,
	MipCountType mip_level_count,
	WW3DFormat texture_format,
	bool allow_compression,
	bool allow_reduction
)
:	TextureClass(0,0,mip_level_count, POOL_MANAGED, false, texture_format)
{
	IsCompressionAllowed=allow_compression;
	InactivationTime=DEFAULT_INACTIVATION_TIME;		// Default inactivation time 30 seconds

	switch (TextureFormat) 
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	case WW3D_FORMAT_U8V8:		// Bumpmap
	case WW3D_FORMAT_L6V5U5:	// Bumpmap
	case WW3D_FORMAT_X8L8V8U8:	// Bumpmap
		// If requesting bumpmap format that isn't available we'll just return the surface in whatever color
		// format the texture file is in. (This is illegal case, the format support should always be queried
		// before creating a bump texture!)
		if (!DX8Wrapper::Is_Initted() || !DX8Wrapper::Get_Current_Caps()->Support_Texture_Format(TextureFormat)) 
		{
			TextureFormat=WW3D_FORMAT_UNKNOWN;
		}
		// If bump format is valid, make sure compression is not allowed so that we don't even attempt to load
		// from a compressed file (quality isn't good enough for bump map). Also disable mipmapping.
		else 
		{
			IsCompressionAllowed=false;
			MipLevelCount=MIP_LEVELS_1;
			Filter.Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);
		}
		break;
	default:	break;
	}

	WWASSERT_PRINT(name && name[0], "TextureClass CTor: NULL or empty texture name\n");
	int len=strlen(name);
	for (int i=0;i<len;++i) 
	{
		if (name[i]=='+') 
		{
			IsLightmap=true;

			// Set bilinear filtering for lightmaps (they are very stretched and
			// low detail so we don't care for anisotropic or trilinear filtering...)
			Filter.Set_Min_Filter(TextureFilterClass::FILTER_TYPE_FAST);
			Filter.Set_Mag_Filter(TextureFilterClass::FILTER_TYPE_FAST);
			if (mip_level_count!=MIP_LEVELS_1) Filter.Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_FAST);
			break;
		}
	}
	Set_Texture_Name(name);
	Set_Full_Path(full_path);
	WWASSERT(name[0]!='\0');
	if (!WW3D::Is_Texturing_Enabled()) 
	{
		Initialized=true;
		Poke_Texture(NULL);
	}

	// Find original size from the thumbnail (but don't create thumbnail texture yet!)
	ThumbnailClass* thumb=ThumbnailManagerClass::Peek_Thumbnail_Instance_From_Any_Manager(Get_Full_Path());
	if (thumb) 
	{
		Width=thumb->Get_Original_Texture_Width();
		Height=thumb->Get_Original_Texture_Height();
 		if (MipLevelCount!=MIP_LEVELS_1) {
 			MipLevelCount=(MipCountType)thumb->Get_Original_Texture_Mip_Level_Count();
 		}
	}

	LastAccessed=WW3D::Get_Sync_Time();

	// If the thumbnails are not enabled, init the texture at this point to avoid stalling when the
	// mesh is rendered.
	if (!WW3D::Get_Thumbnail_Enabled()) 
	{
		if (TextureLoader::Is_DX8_Thread()) 
		{
			Init();
		}
	}
}

// don't know if these are needed
#if 0
// ----------------------------------------------------------------------------
CubeTextureClass::CubeTextureClass
(
	SurfaceClass *surface, 
	MipCountType mip_level_count
)
:	TextureClass(0,0,mip_level_count, POOL_MANAGED, false, surface->Get_Surface_Format())
{
	IsProcedural=true;
	Initialized=true;
	IsReducible=false;

	SurfaceClass::SurfaceDescription sd;
	surface->Get_Description(sd);
	Width=sd.Width;
	Height=sd.Height;
	switch (sd.Format) 
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default: break;
	}

	Poke_Texture
	(
		DX8Wrapper::_Create_DX8_Cube_Texture
		(
			surface->Peek_D3D_Surface(), 
			mip_level_count
		)
	);
	LastAccessed=WW3D::Get_Sync_Time();
}

// ----------------------------------------------------------------------------
CubeTextureClass::CubeTextureClass(IDirect3DBaseTexture8* d3d_texture)
:	TextureBaseClass
	(
		0,
		0,
		((MipCountType)d3d_texture->GetLevelCount())
	),
	Filter((MipCountType)d3d_texture->GetLevelCount())
{
	Initialized=true;
	IsProcedural=true;
	IsReducible=false;

	Peek_Texture()->AddRef();
	IDirect3DSurface8* surface;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(0,&surface));
	D3DSURFACE_DESC d3d_desc;
	::ZeroMemory(&d3d_desc, sizeof(D3DSURFACE_DESC));
	DX8_ErrorCode(surface->GetDesc(&d3d_desc));
	Width=d3d_desc.Width;
	Height=d3d_desc.Height;
	TextureFormat=D3DFormat_To_WW3DFormat(d3d_desc.Format);
	switch (TextureFormat) 
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default: break;
	}

	LastAccessed=WW3D::Get_Sync_Time();
}
#endif

//**********************************************************************************************
//! Apply new surface to texture
/*! 
*/
void CubeTextureClass::Apply_New_Surface
(
	IDirect3DBaseTexture8* d3d_texture,
	bool initialized,
	bool disable_auto_invalidation
)
{
	IDirect3DBaseTexture8* d3d_tex=Peek_D3D_Base_Texture();

	if (d3d_tex) d3d_tex->Release();

	Poke_Texture(d3d_texture);//TextureLoadTask->Peek_D3D_Texture();
	d3d_texture->AddRef();

	if (initialized) Initialized=true;
	if (disable_auto_invalidation) InactivationTime = 0;

	WWASSERT(d3d_texture);
	D3DSURFACE_DESC d3d_desc;
	::ZeroMemory(&d3d_desc, sizeof(D3DSURFACE_DESC));
	DX8_ErrorCode(Peek_D3D_CubeTexture()->GetLevelDesc(0,&d3d_desc));

	if (initialized) 
	{
		TextureFormat=D3DFormat_To_WW3DFormat(d3d_desc.Format);
		Width=d3d_desc.Width;
		Height=d3d_desc.Height;
	}
}


/*************************************************************************
**                             VolumeTextureClass
*************************************************************************/
VolumeTextureClass::VolumeTextureClass
(
	unsigned width, 
	unsigned height, 
	unsigned depth,
	WW3DFormat format, 
	MipCountType mip_level_count, 
	PoolType pool,
	bool rendertarget,
	bool allow_reduction
)
: TextureClass(width, height, format, mip_level_count, pool, rendertarget),
  Depth(depth)
{
	Initialized=true;
	IsProcedural=true;
	IsReducible=false;

	switch (format) 
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default : break;
	}
		
	D3DPOOL d3dpool=(D3DPOOL)0;
	switch(pool)
	{
	case POOL_DEFAULT		: d3dpool=D3DPOOL_DEFAULT; break;
	case POOL_MANAGED		: d3dpool=D3DPOOL_MANAGED; break;
	case POOL_SYSTEMMEM	: d3dpool=D3DPOOL_SYSTEMMEM; break;
	default: WWASSERT(0);
	}

	Poke_Texture
	(
		DX8Wrapper::_Create_DX8_Volume_Texture
		(
			width, 
			height, 
			depth,
			format, 
			mip_level_count,
			d3dpool
		)
	);

	if (pool==POOL_DEFAULT)
	{
		Set_Dirty();
		DX8TextureTrackerClass *track=new DX8TextureTrackerClass
		(
			width, 
			height, 
			format, 
			mip_level_count,
			this,
			rendertarget
		);
		DX8TextureManagerClass::Add(track);
	}
	LastAccessed=WW3D::Get_Sync_Time();
}



// ----------------------------------------------------------------------------
VolumeTextureClass::VolumeTextureClass
(
	const char *name,
	const char *full_path,
	MipCountType mip_level_count,
	WW3DFormat texture_format,
	bool allow_compression,
	bool allow_reduction
)
:	TextureClass(0,0,mip_level_count, POOL_MANAGED, false, texture_format),
	Depth(0)
{
	IsCompressionAllowed=allow_compression;
	InactivationTime=DEFAULT_INACTIVATION_TIME;		// Default inactivation time 30 seconds

	switch (TextureFormat) 
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	case WW3D_FORMAT_U8V8:		// Bumpmap
	case WW3D_FORMAT_L6V5U5:	// Bumpmap
	case WW3D_FORMAT_X8L8V8U8:	// Bumpmap
		// If requesting bumpmap format that isn't available we'll just return the surface in whatever color
		// format the texture file is in. (This is illegal case, the format support should always be queried
		// before creating a bump texture!)
		if (!DX8Wrapper::Is_Initted() || !DX8Wrapper::Get_Current_Caps()->Support_Texture_Format(TextureFormat)) 
		{
			TextureFormat=WW3D_FORMAT_UNKNOWN;
		}
		// If bump format is valid, make sure compression is not allowed so that we don't even attempt to load
		// from a compressed file (quality isn't good enough for bump map). Also disable mipmapping.
		else 
		{
			IsCompressionAllowed=false;
			MipLevelCount=MIP_LEVELS_1;
			Filter.Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);
		}
		break;
	default:	break;
	}

	WWASSERT_PRINT(name && name[0], "TextureClass CTor: NULL or empty texture name\n");
	int len=strlen(name);
	for (int i=0;i<len;++i) 
	{
		if (name[i]=='+') 
		{
			IsLightmap=true;

			// Set bilinear filtering for lightmaps (they are very stretched and
			// low detail so we don't care for anisotropic or trilinear filtering...)
			Filter.Set_Min_Filter(TextureFilterClass::FILTER_TYPE_FAST);
			Filter.Set_Mag_Filter(TextureFilterClass::FILTER_TYPE_FAST);
			if (mip_level_count!=MIP_LEVELS_1) Filter.Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_FAST);
			break;
		}
	}
	Set_Texture_Name(name);
	Set_Full_Path(full_path);
	WWASSERT(name[0]!='\0');
	if (!WW3D::Is_Texturing_Enabled()) 
	{
		Initialized=true;
		Poke_Texture(NULL);
	}

	// Find original size from the thumbnail (but don't create thumbnail texture yet!)
	ThumbnailClass* thumb=ThumbnailManagerClass::Peek_Thumbnail_Instance_From_Any_Manager(Get_Full_Path());
	if (thumb) 
	{
		Width=thumb->Get_Original_Texture_Width();
		Height=thumb->Get_Original_Texture_Height();
 		if (MipLevelCount!=MIP_LEVELS_1) {
 			MipLevelCount=(MipCountType)thumb->Get_Original_Texture_Mip_Level_Count();
 		}
	}

	LastAccessed=WW3D::Get_Sync_Time();

	// If the thumbnails are not enabled, init the texture at this point to avoid stalling when the
	// mesh is rendered.
	if (!WW3D::Get_Thumbnail_Enabled()) 
	{
		if (TextureLoader::Is_DX8_Thread()) 
		{
			Init();
		}
	}
}

// don't know if these are needed
#if 0
// ----------------------------------------------------------------------------
CubeTextureClass::CubeTextureClass
(
	SurfaceClass *surface, 
	MipCountType mip_level_count
)
:	TextureClass(0,0,mip_level_count, POOL_MANAGED, false, surface->Get_Surface_Format())
{
	IsProcedural=true;
	Initialized=true;
	IsReducible=false;

	SurfaceClass::SurfaceDescription sd;
	surface->Get_Description(sd);
	Width=sd.Width;
	Height=sd.Height;
	switch (sd.Format) 
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default: break;
	}

	Poke_Texture
	(
		DX8Wrapper::_Create_DX8_Cube_Texture
		(
			surface->Peek_D3D_Surface(), 
			mip_level_count
		)
	);
	LastAccessed=WW3D::Get_Sync_Time();
}

// ----------------------------------------------------------------------------
CubeTextureClass::CubeTextureClass(IDirect3DBaseTexture8* d3d_texture)
:	TextureBaseClass
	(
		0,
		0,
		((MipCountType)d3d_texture->GetLevelCount())
	),
	Filter((MipCountType)d3d_texture->GetLevelCount())
{
	Initialized=true;
	IsProcedural=true;
	IsReducible=false;

	Peek_Texture()->AddRef();
	IDirect3DSurface8* surface;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(0,&surface));
	D3DSURFACE_DESC d3d_desc;
	::ZeroMemory(&d3d_desc, sizeof(D3DSURFACE_DESC));
	DX8_ErrorCode(surface->GetDesc(&d3d_desc));
	Width=d3d_desc.Width;
	Height=d3d_desc.Height;
	TextureFormat=D3DFormat_To_WW3DFormat(d3d_desc.Format);
	switch (TextureFormat) 
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default: break;
	}

	LastAccessed=WW3D::Get_Sync_Time();
}
#endif




//**********************************************************************************************
//! Apply new surface to texture
/*! 
*/
void VolumeTextureClass::Apply_New_Surface
(
	IDirect3DBaseTexture8* d3d_texture,
	bool initialized,
	bool disable_auto_invalidation
)
{
	IDirect3DBaseTexture8* d3d_tex=Peek_D3D_Base_Texture();

	if (d3d_tex) d3d_tex->Release();

	Poke_Texture(d3d_texture);//TextureLoadTask->Peek_D3D_Texture();
	d3d_texture->AddRef();

	if (initialized) Initialized=true;
	if (disable_auto_invalidation) InactivationTime = 0;

	WWASSERT(d3d_texture);
	D3DVOLUME_DESC d3d_desc;
	::ZeroMemory(&d3d_desc, sizeof(D3DVOLUME_DESC));

	DX8_ErrorCode(Peek_D3D_VolumeTexture()->GetLevelDesc(0,&d3d_desc));

	if (initialized)
	{
		TextureFormat=D3DFormat_To_WW3DFormat(d3d_desc.Format);
		Width=d3d_desc.Width;
		Height=d3d_desc.Height;
		Depth=d3d_desc.Depth;
	}
}
