//----------------------------------------------------------------------------------
// File:        es3aep-kepler\HDR/HDR.cpp
// SDK Version: v3.00
// Email:       gameworks@nvidia.com
// Site:        http://developer.nvidia.com/
//
// Copyright (c) 2014-2015, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//----------------------------------------------------------------------------------

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <SDL.h>

#include "HDR.h"
//#include "NvAppBase/NvFramerateCounter.h"
//#include "NvAppBase/NvInputTransformer.h"
#include "NvAssetLoader.h"
#include "NvGLSLProgram.h"
#include "NvImageGL.h"
//#include "NvTweakBar.h"
#include "NvLogs.h"

#include "ColorModulation.h"

#ifndef _WIN32
#include <unistd.h>
#endif

#ifndef GL_RGBA16F
#define GL_RGBA16F GL_RGBA16F_ARB
#endif


#include "AppExtensions.h"

#ifdef _WIN32
#include <Windows.h>
void NVPlatformLog( const char* fmt, ... )
{
	const int length = 1024;
	char buffer[length];
	va_list ap;
	
	va_start( ap, fmt );
	vsnprintf_s( buffer, length - 1, fmt, ap );
	OutputDebugStringA( buffer );
	OutputDebugStringA( "\n" );
	
	//if( sUseStderr )
	//	fprintf( stderr, "%s\n", buffer );
	
	va_end( ap );
}
#endif

// rendering defs /////////////////////////////////////////////////////////////////////////
#define Z_NEAR 0.4f
#define Z_FAR 500.0f
#define FOV 3.14f*0.5f

const char* model_file[3] = {"models/venus", "models/teapot", "models/knot"};
const char* s_hdr_tex[4] = {"textures/rnl_cross_mmp_s.hdr", "textures/grace_cross_mmp_s.hdr", "textures/altar_cross_mmp_s.hdr", "textures/uffizi_cross_mmp_s.hdr"};
const char* s_hdr_tex_rough[4] = {"textures/rnl_cross_rough_mmp_s.hdr", "textures/grace_cross_rough_mmp_s.hdr", "textures/altar_cross_rough_mmp_s.hdr", "textures/uffizi_cross_rough_mmp_s.hdr"};
const char* s_hdr_tex_irrad[4] = {"textures/rnl_cross_irrad_mmp_s.hdr", "textures/grace_cross_irrad_mmp_s.hdr", "textures/altar_cross_irrad_mmp_s.hdr", "textures/uffizi_cross_irrad_mmp_s.hdr"};
const char* maskTex = {"textures/mask.dds"};

int			m_width = 1920;
int			m_height = 1080;

HDRImage*	image[4] = {0, 0, 0, 0};
GLuint		hdr_tex[4];
HDRImage*	image_rough[4] = {0, 0, 0, 0};
GLuint		hdr_tex_rough[4];
HDRImage*	image_irrad[4] = {0, 0, 0, 0};
GLuint		hdr_tex_irrad[4];

float cameraMixCoeff[4] = {1.2, 0.8, 0.1, 0.0};
float filmicMixCoeff[4] = {0.6, 0.55, 0.08, 0.0};
float exposureCompansation[4] = {3.0, 3.0, 10.0, 4.0};
hfloat exposureInfo[256];

struct MTLData
{
	MTLData( int _type, float _r, float _g, float _b, float _a )
	{
		type = _type;
		r = _r;
		g = _g;
		b = _b;
		a = _a;
	}
	int type;
	float r;
	float g;
	float b;
	float a;
};

enum MATERIAL_TYPE
{
	MATERIAL_MAT		= 0x00000001,
	MATERIAL_REFRACT	= 0x00000002,
	MATERIAL_REFLECT	= 0x00000003,
	MATERIAL_MATTE		= 0x00000011,
	MATERIAL_ALUM		= 0x00000013,
	MATERIAL_SILVER		= 0x00000023,
	MATERIAL_GOLDEN		= 0x00000033,
	MATERIAL_METALIC	= 0x00000043,
	MATERIAL_DIAMOND	= 0x00000012,
	MATERIAL_EMERALD	= 0x00000022,
	MATERIAL_RUBY		= 0x00000032,
};

MTLData material[8] =
{
	MTLData( MATERIAL_MATTE, 1.0, 1.0, 1.0, 0.0 ),
	MTLData( MATERIAL_ALUM, 1.0, 1.0, 1.0, 0.5 ),
	MTLData( MATERIAL_SILVER, 1.0, 1.0, 1.0, 0.9 ),
	MTLData( MATERIAL_GOLDEN, 1.0, 0.9, 0.4, 0.9 ),
	MTLData( MATERIAL_METALIC, 1.0, 1.0, 1.0, 0.1 ),
	MTLData( MATERIAL_DIAMOND, 0.8, 0.8, 0.8, 1.0 ),
	MTLData( MATERIAL_EMERALD, 0.2, 0.8, 0.2, 1.0 ),
	MTLData( MATERIAL_RUBY, 0.9, 0.1, 0.4, 1.0 ),
};

HDR::HDR()
{
	m_transformer = new NvInputTransformer;
	m_transformer->setScreenSize( m_width, m_height );
	m_transformer->setTranslationVec( nv::vec3f( 0.0f, 0.0f, -202.2f ) );
	m_transformer->setRotationVec( nv::vec3f( 0.0f, 0.0f, 0.0f ) );
	
	// Required in all subclasses to avoid silent link issues
	//forceLinkHack();
	
	m_drawHDR = true;
	m_drawBackground = true;
	m_autoSpin = true;
	m_autoExposure = true;
	m_sceneIndex = 2;
	m_objectIndex = 0;
	m_materialIndex = 5;
	m_glareType = CAMERA_GLARE;
	m_bInitialized = 0;
	m_gamma = 1.0 / 1.8;
	m_blendAmount = 0.33;
	m_lumThreshold = 1.0;
	m_lumScaler = 0.30;
	m_starGenLevel = LEVEL_0;
	m_expAdjust = 1.4;
	
}

void HDR::DrawAxisAlignedQuad( float afLowerLeftX, float afLowerLeftY, float afUpperRightX, float afUpperRightY )
{
	glBindVertexArray( 0 );
	
	//glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	//glBindBuffer( GL_ARRAY_BUFFER, 0 );
	
	glDisable( GL_DEPTH_TEST );
	
	glEnableVertexAttribArray( 0 );
	glEnableVertexAttribArray( 1 );
	
	const float afVertexData[] = { afLowerLeftX, afLowerLeftY, 0.0,    afUpperRightX, afLowerLeftY, 0.0,
								   afLowerLeftX, afUpperRightY, 0.0,    afUpperRightX, afUpperRightY, 0.0
								 };
	glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, 0, afVertexData );
	
	const float afTexCoordData[] = { 0, 0,  1, 0,  0, 1,  1, 1 };
	glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, 0, afTexCoordData );
	
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	
	glDisableVertexAttribArray( 0 );
	glDisableVertexAttribArray( 1 );
	
	glBindVertexArray( m_VAO );
}

GLuint createCubemapTexture( HDRImage& img, GLint internalformat, bool filtering = true )
{

	GLuint tex;
	
	glGenTextures( 1, &tex );
	glBindTexture( GL_TEXTURE_CUBE_MAP, tex );
	
	glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, filtering ? GL_LINEAR : GL_NEAREST );
	glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, filtering ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST );
	glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	
	hfloat* out = new hfloat[img.getWidth()*img.getHeight() * 3];
	for( int i = 0; i < 6; i++ )
	{
		FP32toFP16( ( float* )img.getLevel( 0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i ), out, img.getWidth(), img.getHeight() );
		
		glTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0,
					  GL_RGB16F, img.getWidth(), img.getHeight(), 0,
					  GL_RGB, GL_HALF_FLOAT, out );
	}
	glGenerateMipmap( GL_TEXTURE_CUBE_MAP );
	delete []out;
	return tex;
}

int LoadMdlDataFromFile( const char* name, void** buffer )
{
	int32_t len;
	char* data = NvAssetLoaderRead( name, len );
	
	if( !data ) return 0;
	*buffer = malloc( len + 1 );
	memcpy( *buffer, data, len );
	NvAssetLoaderFree( data );
	
	return len;
}

void printGLString( const char* name, GLenum s )
{
	char* v = ( char* ) glGetString( s );
	LOGI( "GL %s: %s\n", name, v );
}

void HDR::GetBufferPyramidSize( BUFFER_PYRAMID level, int* w, int* h )
{
	int width = m_postProcessingWidth / 4;
	int height = m_postProcessingHeight / 4;
	int lvl = level;
	while( lvl > LEVEL_0 )
	{
		width /= 2;
		height /= 2;
		lvl--;
	}
	*w = width;
	*h = height;
}

HDR::~HDR()
{
	LOGI( "HDR: destroyed\n" );
}

/*
void HDR::configurationCallback(NvGLConfiguration& config)
{
    config.depthBits = 24;
    config.stencilBits = 0;
    config.apiVer =  NvGLAPIVersionES3_1();
}
*/

void HDR::InitRenderTexture( int width, int height )
{
	m_postProcessingWidth = 1024;
	m_postProcessingHeight = 1024;
	
	//renderBuffer for the whole scene
	scene_buffer = new RenderTexture();
	scene_buffer->Init( width, height, RenderTexture::RGBA16F, RenderTexture::Depth24 );
	
	int w = m_postProcessingWidth / 4;
	int h = m_postProcessingHeight / 4;
	
	//renderBuffer for final glare composition
	glare_buffer = new RenderTexture();
	glare_buffer->Init( m_postProcessingWidth / 2, m_postProcessingHeight / 2, RenderTexture::RGBA16F, RenderTexture::NoDepth );
	
	//buffer pyramid for wide range gaussian blur
	for( int i = 0; i < LEVEL_TOTAL; i++ )
	{
		compose_buffer[i] = new RenderTexture();
		compose_buffer[i]->Init( w, h, RenderTexture::RGBA16F, RenderTexture::NoDepth );
		
		blur_bufferA[i] = new RenderTexture();
		blur_bufferA[i]->Init( w, h, RenderTexture::RGBA16F, RenderTexture::NoDepth );
		
		blur_bufferB[i] = new RenderTexture();
		blur_bufferB[i]->Init( w, h, RenderTexture::RGBA16F, RenderTexture::NoDepth );
		
		w /= 2;
		h /= 2;
	}
	
	//get resolution for star streak rendering
	GetBufferPyramidSize( m_starGenLevel, &w, &h );
	
	//4 directions
	for( int i = 0; i < 4; i++ )
	{
		streak_bufferA[i] = new RenderTexture();
		streak_bufferA[i]->Init( w, h, RenderTexture::RGBA16F, RenderTexture::NoDepth );
		
		streak_bufferB[i] = new RenderTexture();
		streak_bufferB[i]->Init( w, h, RenderTexture::RGBA16F, RenderTexture::NoDepth );
	}
	
	//star streak composition renderbuffer
	streak_bufferFinal = new RenderTexture();
	streak_bufferFinal->Init( w, h, RenderTexture::RGBA16F, RenderTexture::NoDepth );
	
	//renderbuffer for ghost image
	ghost1st_buffer = new RenderTexture();
	ghost1st_buffer->Init( m_postProcessingWidth / 2, m_postProcessingHeight / 2, RenderTexture::RGBA16F, RenderTexture::NoDepth );
	
	ghost2nd_buffer = new RenderTexture();
	ghost2nd_buffer->Init( m_postProcessingWidth / 2, m_postProcessingHeight / 2, RenderTexture::RGBA16F, RenderTexture::NoDepth );
	
	w = m_postProcessingWidth / 16;
	h = m_postProcessingHeight / 16;
	for( int i = 0; i < 2; i++ )
	{
		//2 buffers for downsampling to get thumbnail image
		exp_buffer[i] = new RenderTexture();
		exp_buffer[i]->Init( w, h, RenderTexture::RGBA16F, RenderTexture::NoDepth );
		
		w /= 4;
		h /= 4;
	}
	
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

void HDR::initBlurCode()
{
	int i;
	float s[4] = {0.42, 0.25, 0.15, 0.10};
	float* weights;
	int width, w, h, id;
	
	//start from quarter size
	w = ( int )( m_postProcessingWidth / 4 * m_aspectRatio );
	h = ( int )( m_postProcessingHeight / 4 );
	
	//gen blur code at each level
	for( i = 0; i < 4; i++ )
	{
		id = BLURH4 + i * 2;
		weights = generateGaussianWeights( s[i], width );
		m_shaders[id].pid = generate1DConvolutionFP_filter( vtx_blur, weights, width, false, true, w, h );
		m_shaders[id + 1].pid = generate1DConvolutionFP_filter( vtx_blur, weights, width, true, true, w, h );
		m_shaders[id].GenLocation( LOC_PARAMETER( blur ) );
		m_shaders[id + 1].GenLocation( LOC_PARAMETER( blur ) );
		delete [] weights;
		w /= 2;
		h /= 2;
	}
}

bool HDR::initShaders()
{
	//bool isES = getGLContext()->getConfiguration().apiVer.api == NvGLAPI::GLES;
	bool isES = false;
	
	//if( getGLContext()->getConfiguration().apiVer.api == NvGLAPI::GL )
	{
		NvGLSLProgram::setGlobalShaderHeader( "#version 130\n" );
	}
	//else
	//{
	//	NvGLSLProgram::setGlobalShaderHeader( "#version 300 es\n" );
	//}
	
	m_shaders[SKYBOX].GenProgram( PROGRAM_PARAMETER( skybox ) );
	m_shaders[MATTEOBJECT].GenProgram( PROGRAM_PARAMETER( matteObject ) );
	m_shaders[REFRACTOBJECT].GenProgram( PROGRAM_PARAMETER( refractObject ) );
	m_shaders[REFLECTOBJECT].GenProgram( PROGRAM_PARAMETER( reflectObject ) );
	m_shaders[DOWNSAMPLE].GenProgram( PROGRAM_PARAMETER( downSample ) );
	m_shaders[DOWNSAMPLE4X].GenProgram( PROGRAM_PARAMETER( downSample4x ) );
	m_shaders[EXTRACTHL].GenProgram( PROGRAM_PARAMETER( extractHL ) );
	m_shaders[TONEMAPPING].GenProgram( PROGRAM_PARAMETER( tonemap ) );
	m_shaders[GAUSCOMP].GenProgram( PROGRAM_PARAMETER( gaussianCompose ) );
	m_shaders[STARSTREAK].GenProgram( PROGRAM_PARAMETER( starStreak ) );
	m_shaders[GLARECOMP].GenProgram( PROGRAM_PARAMETER( glareCompose ) );
	m_shaders[STARCOMP].GenProgram( PROGRAM_PARAMETER( starStreakCompose ) );
	m_shaders[GHOSTIMAGE].GenProgram( PROGRAM_PARAMETER( ghostImage ) );
	
	initBlurCode();
	
	NvGLSLProgram::setGlobalShaderHeader( NULL );
	
	m_shaders[CALCLUMINANCE].GenCSProgram(
		isES ? cs_calculateLuminanceES : cs_calculateLuminance );
	m_shaders[CALCADAPTEDLUM].GenCSProgram(
		isES ? cs_calculateAdaptedLumES : cs_calculateAdaptedLum, uni_calculateAdaptedLum, 1 );
		
	return true;
}

bool HDR::initModels()
{
	int i;
	void* pData = 0;
	
	for( i = 0; i < 3; i++ )
	{
		m_objectFileSize[i] = LoadMdlDataFromFile( model_file[i], &pData );
		m_object[i].GenVertexData( pData, m_objectFileSize[i] );
		free( pData );
	}
	
	m_skybox.GenVertexData( verticesCube, 4 * 8 * 6 * 4 );
	m_skybox.GenIndexData( indicesCube, 6 * 6 * 2 );
	
	return true;
}

void HDR::initRendering()
{
	//NV_APP_BASE_SHARED_INIT();
	
	printGLString( "Version",    GL_VERSION );
	printGLString( "Vendor",     GL_VENDOR );
	printGLString( "Renderer",   GL_RENDERER );
	//printGLString( "Extensions", GL_EXTENSIONS );
	NvAssetLoaderAddSearchPath( "es3aep-kepler/HDR" );
	
	glGenVertexArrays( 1, &m_VAO );
	glBindVertexArray( m_VAO );
	
	GLuint m_lensMask;
	
	int i;
	
	//GLint depthBits;
	//glGetIntegerv( GL_DEPTH_BITS, &depthBits );
	//LOGI( "depth bits = %d\n", depthBits );
	
	glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
	
	InitRenderTexture( m_width, m_height );
	m_aspectRatio = float( m_width ) / float( m_height );
	
	//load all HDRImages we need
	for( i = 0; i < 4; i++ )
	{
		image[i] = new HDRImage;
		if( !image[i]->loadHDRIFromFile( s_hdr_tex[i] ) )
		{
			fprintf( stderr, "Error loading image file '%s'\n", s_hdr_tex[i] );
			exit( -1 );
		}
		
		if( !image[i]->convertCrossToCubemap() )
		{
			fprintf( stderr, "Error converting image to cubemap\n" );
			exit( -1 );
		};
		
		hdr_tex[i] = createCubemapTexture( *image[i], GL_RGB );
	}
	for( i = 0; i < 4; i++ )
	{
		image_rough[i] = new HDRImage;
		if( !image_rough[i]->loadHDRIFromFile( s_hdr_tex_rough[i] ) )
		{
			fprintf( stderr, "Error loading image file '%s'\n", s_hdr_tex_rough[i] );
			exit( -1 );
		}
		
		if( !image_rough[i]->convertCrossToCubemap() )
		{
			fprintf( stderr, "Error converting image to cubemap\n" );
			exit( -1 );
		};
		
		hdr_tex_rough[i] = createCubemapTexture( *image_rough[i], GL_RGB );
	}
	for( i = 0; i < 4; i++ )
	{
		image_irrad[i] = new HDRImage;
		if( !image_irrad[i]->loadHDRIFromFile( s_hdr_tex_irrad[i] ) )
		{
			fprintf( stderr, "Error loading image file '%s'\n", s_hdr_tex_irrad[i] );
			exit( -1 );
		}
		
		if( !image_irrad[i]->convertCrossToCubemap() )
		{
			fprintf( stderr, "Error converting image to cubemap\n" );
			exit( -1 );
		};
		hdr_tex_irrad[i] = createCubemapTexture( *image_irrad[i], GL_RGB );
	}
	
	//load mask texture for ghost image generation
	m_lensMask = NvImageGL::UploadTextureFromDDSFile( maskTex );
	glBindTexture( GL_TEXTURE_2D, m_lensMask );
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	
	glGenTextures( 1, &m_lumCurrent );
	glBindTexture( GL_TEXTURE_2D, m_lumCurrent );
	glTexStorage2D( GL_TEXTURE_2D, 1, GL_RGBA16F, 1, 1 );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	
	for( i = 0; i < 2; i++ )
	{
		glGenTextures( 1, &m_lum[i] );
		glBindTexture( GL_TEXTURE_2D, m_lum[i] );
		glTexStorage2D( GL_TEXTURE_2D, 1, GL_RGBA16F, 1, 1 );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	}
	glBindTexture( GL_TEXTURE_2D, 0 );
	
	initShaders();
	
	initModels();
	
	modulateColor();
}

void HDR::prepareImGui()
{
	// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
	if( m_showDemoWindow )
	{
		ImGui::ShowDemoWindow( &m_showDemoWindow );
	}
	
	// 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
	{
		static float f = 0.0f;
		static int counter = 0;
		
		ImGui::Begin( "HDR Sample" );                        // Create a window called "Hello, world!" and append into it.
		
		//ImGui::Text( "This is some useful text." );             // Display some text (you can use a format strings too)
		ImGui::Checkbox( "Demo Window", &m_showDemoWindow );    // Edit bools storing our window open/close state
		
		ImGui::Checkbox( "Auto Spin", &m_autoSpin );
		ImGui::Checkbox( "Draw Background", &m_drawBackground );
		
		ImGui::Checkbox( "Adaptive Exposure", &m_autoExposure );
		
		ImGui::SliderFloat( "Exposure", &m_expAdjust, 1.0f, 5.0f );
		
		ImGui::ColorEdit3( "Clear Color", ( float* )&m_clearColor );
		
		//if( ImGui::Button( "Button" ) )                         // Buttons return true when clicked (most widgets return true when edited/activated)
		//	counter++;
		//ImGui::SameLine();
		//ImGui::Text( "counter = %d", counter );
		
		static int itemCurrent = m_sceneIndex;
		ImGui::Combo( "Scene", &itemCurrent, "Nature\0Grace\0Altar\0Uffizi\0\0" );
		m_sceneIndex = itemCurrent;
		
		static int itemCurrent2 = m_materialIndex;
		ImGui::Combo( "Material", &itemCurrent2, "Matte\0Alum\0Silver\0Golden\0Metalic\0Diamond\0Emerald\0Ruby\0\0" );
		m_materialIndex = itemCurrent2;
		
		static int itemCurrent3 = m_objectIndex;
		ImGui::Combo( "Object", &itemCurrent3, "Venus\0Teapot\0Knot\0\0" );
		m_objectIndex = itemCurrent3;
		
		static int itemCurrent4 = m_objectIndex;
		ImGui::Combo( "Glare Type", &itemCurrent4, "Camera\0Filmic\0\0" );
		
		m_glareType = CAMERA_GLARE;
		if( itemCurrent4 == 1 )
		{
			m_glareType = FILMIC_GLARE;
		}
		
		ImGui::Text( "Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate );
		ImGui::End();
	}
}


void HDR::reshape( int32_t width, int32_t height )
{
	glViewport( 0, 0, ( GLint ) width, ( GLint ) height );
	
	//CHECK_GL_ERROR();
}

void HDR::DrawSkyBox()
{
	glUseProgram( m_shaders[SKYBOX].pid );
	
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_CULL_FACE );
	
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_CUBE_MAP, hdr_tex[m_sceneIndex] );
	
	nv::matrix4f view = inverse( m_transformer->getModelViewMat() );
	
	glUniformMatrix4fv( m_shaders[SKYBOX].auiLocation[0], 1, GL_FALSE, view._array );
	glUniformMatrix4fv( m_shaders[SKYBOX].auiLocation[1], 1, GL_FALSE, projection_matrix._array );
	
	glBindBuffer( GL_ARRAY_BUFFER, m_skybox.GetVBO() );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, m_skybox.GetIBO() );
	
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer( 0, 3, GL_FLOAT, false, 32, 0 );
	
	glDrawElements( GL_TRIANGLES, 6 * 6, GL_UNSIGNED_SHORT, 0 );
	
	glDisableVertexAttribArray( 0 );
	
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
}

void HDR::DrawObject()
{
	glEnable( GL_DEPTH_TEST );
	glEnable( GL_CULL_FACE );
	glFrontFace( GL_CCW );
	glCullFace( GL_FRONT );
	int mtlClass = material[m_materialIndex].type & 0xF;
	
	glUseProgram( m_shaders[mtlClass].pid );
	
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_CUBE_MAP, hdr_tex[m_sceneIndex] );
	switch( mtlClass )
	{
		case MATERIAL_MAT:
			glActiveTexture( GL_TEXTURE1 );
			glBindTexture( GL_TEXTURE_CUBE_MAP, hdr_tex_irrad[m_sceneIndex] );
			break;
		case MATERIAL_REFLECT:
			glActiveTexture( GL_TEXTURE1 );
			glBindTexture( GL_TEXTURE_CUBE_MAP, hdr_tex_rough[m_sceneIndex] );
			break;
		case MATERIAL_REFRACT:
			break;
	}
	
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_CUBE_MAP, hdr_tex[m_sceneIndex] );
	nv::matrix4f model_matrix;
	nv::matrix4f view_matrix = m_transformer->getModelViewMat();
	nv::matrix4f view_inv = inverse( view_matrix );
	model_matrix.make_identity();
	// calculate eye position in cubemap space
	nv::vec4f eyePos_eye( 0.0, 0.0, 0.0, 1.0 ), eyePos_model;
	
	eyePos_model = view_inv * eyePos_eye;
	nv::matrix4f mvp = projection_matrix * view_matrix;
	
	float emission[3] = {0.0, 0.0, 0.0};
	
	glUniformMatrix4fv( m_shaders[mtlClass].auiLocation[0], 1, GL_FALSE, mvp._array );
	glUniformMatrix4fv( m_shaders[mtlClass].auiLocation[1], 1, GL_FALSE, model_matrix._array );
	glUniform3fv( m_shaders[mtlClass].auiLocation[2], 1, &eyePos_model.x );
	glUniform3fv( m_shaders[mtlClass].auiLocation[3], 1, emission );
	glUniform4fv( m_shaders[mtlClass].auiLocation[4], 1, &material[m_materialIndex].r );
	
	glBindBuffer( GL_ARRAY_BUFFER, m_object[m_objectIndex].GetVBO() );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	
	glEnableVertexAttribArray( 0 );
	glEnableVertexAttribArray( 1 );
	
	glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, 32, 0 );
	glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, 32, ( void* )12 );
	glVertexAttribPointer( 2, 3, GL_FLOAT, GL_FALSE, 32, ( void* )24 );
	glDrawArrays( GL_TRIANGLES, 0, m_objectFileSize[m_objectIndex] / 32 );
	
	glDisableVertexAttribArray( 0 );
	glDisableVertexAttribArray( 1 );
	
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
}

void HDR::DrawScene()
{
	scene_buffer->ActivateFB();
	
	//we only need to clear depth
	glClear( GL_DEPTH_BUFFER_BIT );
	
	if( m_drawBackground )
		DrawSkyBox();
	else
		glClear( GL_COLOR_BUFFER_BIT );
	DrawObject();
}


// downsample image 2x in each dimension
void HDR::downsample( RenderTexture* src, RenderTexture* dest )
{
	dest->ActivateFB();
	
	glUseProgram( m_shaders[DOWNSAMPLE].pid );
	glDisable( GL_CULL_FACE );
	glDisable( GL_BLEND );
	
	glActiveTexture( GL_TEXTURE0 );
	src->Bind();
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
	src->Release();
	
}

// downsample image 4x in each dimension
void HDR::downsample4x( RenderTexture* src, RenderTexture* dest )
{
	dest->ActivateFB();
	
	glUseProgram( m_shaders[DOWNSAMPLE4X].pid );
	glDisable( GL_CULL_FACE );
	glDisable( GL_BLEND );
	float twoPixelX = float( 2.0 ) / src->GetWidth();
	float twoPixelY = float( 2.0 ) / src->GetHeight();
	
	glUniform2f( m_shaders[DOWNSAMPLE4X].auiLocation[0], twoPixelX, twoPixelY );
	
	glActiveTexture( GL_TEXTURE0 );
	src->Bind();
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
	src->Release();
	
}

// extract high light from scene
void HDR::extractHL( RenderTexture* src, RenderTexture* dest )
{
	dest->ActivateFB();
	
	glUseProgram( m_shaders[EXTRACTHL].pid );
	glDisable( GL_CULL_FACE );
	glDisable( GL_BLEND );
	
	glUniform1f( m_shaders[EXTRACTHL].auiLocation[0], m_lumThreshold );
	glUniform1f( m_shaders[EXTRACTHL].auiLocation[1], m_lumScaler );
	
	glActiveTexture( GL_TEXTURE0 );
	src->Bind();
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
	src->Release();
	
	
}

void HDR::run_pass( HDR_PASS prog, RenderTexture* src, RenderTexture* dest )
{
	dest->ActivateFB();
	
	glUseProgram( m_shaders[prog].pid );
	
	glDisable( GL_CULL_FACE );
	glDisable( GL_BLEND );
	
	glActiveTexture( GL_TEXTURE0 );
	src->Bind();
	
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
	
	src->Release();
	
	
}

void HDR::blur( RenderTexture* src, RenderTexture* dest, RenderTexture* temp, int blurWidth )
{
	run_pass( ( HDR_PASS )( blurWidth ), src, temp );
	run_pass( ( HDR_PASS )( blurWidth + 1 ), temp, dest );
}

void HDR::ComposeEffect()
{
	////////compose gaussian blur buffers///////////////
	compose_buffer[LEVEL_0]->ActivateFB();
	glUseProgram( m_shaders[GAUSCOMP].pid );
	
	glActiveTexture( GL_TEXTURE0 );
	blur_bufferA[LEVEL_0]->Bind();
	glActiveTexture( GL_TEXTURE1 );
	blur_bufferA[LEVEL_1]->Bind();
	glActiveTexture( GL_TEXTURE2 );
	blur_bufferA[LEVEL_2]->Bind();
	glActiveTexture( GL_TEXTURE3 );
	blur_bufferA[LEVEL_3]->Bind();
	
	float coeff[4] = {0.3, 0.3, 0.25, 0.20};
	
	glUniform4fv( m_shaders[GAUSCOMP].auiLocation[0], 1, coeff );
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
	
	////////compose star streak from 4 directions///////////////
	streak_bufferFinal->ActivateFB();
	glUseProgram( m_shaders[STARCOMP].pid );
	
	glActiveTexture( GL_TEXTURE0 );
	streak_bufferA[0]->Bind();
	glActiveTexture( GL_TEXTURE1 );
	streak_bufferA[1]->Bind();
	
	if( m_glareType == CAMERA_GLARE )
	{
		glActiveTexture( GL_TEXTURE2 );
		streak_bufferA[2]->Bind();
		glActiveTexture( GL_TEXTURE3 );
		streak_bufferA[3]->Bind();
	}
	else if( m_glareType == FILMIC_GLARE )
	{
		glActiveTexture( GL_TEXTURE2 );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glActiveTexture( GL_TEXTURE3 );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}
	
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
	
	////////////////final glare composition/////////////
	glare_buffer->ActivateFB();
	glUseProgram( m_shaders[GLARECOMP].pid );
	
	glActiveTexture( GL_TEXTURE0 );
	compose_buffer[LEVEL_0]->Bind();
	glActiveTexture( GL_TEXTURE1 );
	streak_bufferFinal->Bind();
	glActiveTexture( GL_TEXTURE2 );
	ghost2nd_buffer->Bind();
	
	float* mixCoeff = cameraMixCoeff;
	if( m_glareType == FILMIC_GLARE ) mixCoeff = filmicMixCoeff;
	else if( m_glareType == CAMERA_GLARE )  mixCoeff = cameraMixCoeff;
	
	glUniform4fv( m_shaders[GLARECOMP].auiLocation[0], 1, mixCoeff );
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
	
}

void HDR::genStarStreak( int dir )
{
#define delta 0.9
	int n, s, w, h;
	float step[2];
	float stride = 1.0;
	GetBufferPyramidSize( m_starGenLevel, &w, &h );
	switch( dir )
	{
		case 0:
			step[1] = float( delta ) / w * m_aspectRatio;
			step[0] = float( delta ) / w;
			break;
		case 1:
			step[1] = float( delta ) / w * m_aspectRatio;
			step[0] = -float( delta ) / w;
			break;
		case 2:
			step[1] = -float( delta ) / w * m_aspectRatio;
			step[0] = float( delta ) / w;
			break;
		case 3:
			step[1] = -float( delta ) / w * m_aspectRatio;
			step[0] = -float( delta ) / w;
			break;
		default:
			break;
	}
	
	glUseProgram( m_shaders[STARSTREAK].pid );
	
#define DEC 0.9
	float colorCoeff[16];
	
	//3 passes to generate 64 pixel blur in each direction
	//1st pass
	n = 1;
	for( s = 0; s < 4; s += 1 )
	{
		colorCoeff[s * 4] = star_modulation1st[s * 4] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 1] = star_modulation1st[s * 4 + 1] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 2] = star_modulation1st[s * 4 + 2] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 3] = star_modulation1st[s * 4 + 3] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
	}
	streak_bufferA[dir]->ActivateFB();
	glActiveTexture( GL_TEXTURE0 );
	compose_buffer[LEVEL_0]->Bind();
	glUniform2fv( m_shaders[STARSTREAK].auiLocation[0], 1, step );
	glUniform1f( m_shaders[STARSTREAK].auiLocation[1], stride );
	glUniform4fv( m_shaders[STARSTREAK].auiLocation[2], 4, colorCoeff );
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
	
	// 2nd pass
	n = 2;
	for( s = 0; s < 4; s += 1 )
	{
		colorCoeff[s * 4] = star_modulation2nd[s * 4] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 1] = star_modulation2nd[s * 4 + 1] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 2] = star_modulation2nd[s * 4 + 2] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 3] = star_modulation2nd[s * 4 + 3] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
	}
	stride = 4;
	streak_bufferB[dir]->ActivateFB();
	glActiveTexture( GL_TEXTURE0 );
	streak_bufferA[dir]->Bind();
	glUniform2fv( m_shaders[STARSTREAK].auiLocation[0], 1, step );
	glUniform1f( m_shaders[STARSTREAK].auiLocation[1], stride );
	glUniform4fv( m_shaders[STARSTREAK].auiLocation[2], 4, colorCoeff );
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
	
	// 3rd pass
	n = 3;
	for( s = 0; s < 4; s += 1 )
	{
		colorCoeff[s * 4] =  star_modulation3rd[s * 4] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 1] = star_modulation3rd[s * 4 + 1] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 2] = star_modulation3rd[s * 4 + 2] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 3] = star_modulation3rd[s * 4 + 3] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
	}
	stride = 16;
	streak_bufferA[dir]->ActivateFB();
	glActiveTexture( GL_TEXTURE0 );
	streak_bufferB[dir]->Bind();
	glUniform2fv( m_shaders[STARSTREAK].auiLocation[0], 1, step );
	glUniform1f( m_shaders[STARSTREAK].auiLocation[1], stride );
	glUniform4fv( m_shaders[STARSTREAK].auiLocation[2], 4, colorCoeff );
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
}


void HDR::genHorizontalGlare( int dir )
{
#define delta 0.9
	int n, s, w, h;
	float step[2];
	float stride = 1.0;
	GetBufferPyramidSize( m_starGenLevel, &w, &h );
	
	if( dir == 0 )
	{
		step[0] = float( delta ) / w;
	}
	else
	{
		step[0] = -float( delta ) / w;
	}
	step[1] = 0;
	
	glUseProgram( m_shaders[STARSTREAK].pid );
	
#undef DEC
#define DEC 0.96
	float colorCoeff[16];
	
	//4 passes to generate 256 pixel blur in each direction
	//1st pass
	n = 1;
	for( s = 0; s < 4; s += 1 )
	{
		colorCoeff[s * 4] = hori_modulation1st[s * 4] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 1] = hori_modulation1st[s * 4 + 1] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 2] = hori_modulation1st[s * 4 + 2] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 3] = hori_modulation1st[s * 4 + 3] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
	}
	streak_bufferB[dir]->ActivateFB();
	glActiveTexture( GL_TEXTURE0 );
	compose_buffer[LEVEL_0]->Bind();
	glUniform2fv( m_shaders[STARSTREAK].auiLocation[0], 1, step );
	glUniform1f( m_shaders[STARSTREAK].auiLocation[1], stride );
	glUniform4fv( m_shaders[STARSTREAK].auiLocation[2], 4, colorCoeff );
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
	
	// 2nd pass
	n = 2;
	for( s = 0; s < 4; s += 1 )
	{
		colorCoeff[s * 4] = hori_modulation1st[s * 4] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 1] = hori_modulation1st[s * 4 + 1] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 2] = hori_modulation1st[s * 4 + 2] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 3] = hori_modulation1st[s * 4 + 3] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
	}
	stride = 4;
	streak_bufferA[dir]->ActivateFB();
	glActiveTexture( GL_TEXTURE0 );
	streak_bufferB[dir]->Bind();
	glUniform2fv( m_shaders[STARSTREAK].auiLocation[0], 1, step );
	glUniform1f( m_shaders[STARSTREAK].auiLocation[1], stride );
	glUniform4fv( m_shaders[STARSTREAK].auiLocation[2], 4, colorCoeff );
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
	
	// 3rd pass
	n = 3;
	for( s = 0; s < 4; s += 1 )
	{
		colorCoeff[s * 4] =  hori_modulation2nd[s * 4] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 1] = hori_modulation2nd[s * 4 + 1] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 2] = hori_modulation2nd[s * 4 + 2] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 3] = hori_modulation2nd[s * 4 + 3] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
	}
	stride = 16;
	streak_bufferB[dir]->ActivateFB();
	glActiveTexture( GL_TEXTURE0 );
	streak_bufferA[dir]->Bind();
	glUniform2fv( m_shaders[STARSTREAK].auiLocation[0], 1, step );
	glUniform1f( m_shaders[STARSTREAK].auiLocation[1], stride );
	glUniform4fv( m_shaders[STARSTREAK].auiLocation[2], 4, colorCoeff );
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
	
	// 4rd pass
	n = 4;
	for( s = 0; s < 4; s += 1 )
	{
		colorCoeff[s * 4] =  hori_modulation3rd[s * 4] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 1] = hori_modulation3rd[s * 4 + 1] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 2] = hori_modulation3rd[s * 4 + 2] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
		colorCoeff[s * 4 + 3] = hori_modulation3rd[s * 4 + 3] * pow( float( DEC ), pow( float( 4 ), n - 1 ) * s );
	}
	stride = 64;
	streak_bufferA[dir]->ActivateFB();
	glActiveTexture( GL_TEXTURE0 );
	streak_bufferB[dir]->Bind();
	glUniform2fv( m_shaders[STARSTREAK].auiLocation[0], 1, step );
	glUniform1f( m_shaders[STARSTREAK].auiLocation[1], stride );
	glUniform4fv( m_shaders[STARSTREAK].auiLocation[2], 4, colorCoeff );
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
}


void HDR::genGhostImage( float* ghost_modulation1st, float* ghost_modulation2nd )
{
	glUseProgram( m_shaders[GHOSTIMAGE].pid );
	
	ghost1st_buffer->ActivateFB();
	
	glActiveTexture( GL_TEXTURE0 );
	blur_bufferA[LEVEL_0]->Bind();
	glActiveTexture( GL_TEXTURE1 );
	blur_bufferA[LEVEL_1]->Bind();
	glActiveTexture( GL_TEXTURE2 );
	blur_bufferA[LEVEL_1]->Bind();
	glActiveTexture( GL_TEXTURE3 );
	glBindTexture( GL_TEXTURE_2D, m_lensMask );
	
	float scalar[4] = { -4.0, 3.0, -2.0, 0.3};
	glUniform4fv( m_shaders[GHOSTIMAGE].auiLocation[0], 1, scalar );
	glUniform4fv( m_shaders[GHOSTIMAGE].auiLocation[1], 4, ghost_modulation1st );
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
	
	ghost2nd_buffer->ActivateFB();
	glActiveTexture( GL_TEXTURE0 );
	ghost1st_buffer->Bind();
	glActiveTexture( GL_TEXTURE1 );
	ghost1st_buffer->Bind();
	glActiveTexture( GL_TEXTURE2 );
	blur_bufferA[LEVEL_1]->Bind();
	glActiveTexture( GL_TEXTURE3 );
	glBindTexture( GL_TEXTURE_2D, m_lensMask );
	
	float scalar2[4] = {3.6, 2.0, 0.9, -0.55};
	glUniform4fv( m_shaders[GHOSTIMAGE].auiLocation[0], 1, scalar2 );
	glUniform4fv( m_shaders[GHOSTIMAGE].auiLocation[1], 4, ghost_modulation2nd );
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
	
	
}

/*
 * read from float texture, apply tone mapping, render to regular RGB888 display
 */
void HDR::toneMappingPass()
{
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	glViewport( 0, 0, m_width, m_height );
	
	
	glUseProgram( m_shaders[TONEMAPPING].pid );
	
	glActiveTexture( GL_TEXTURE0 );
	scene_buffer->Bind();
	glActiveTexture( GL_TEXTURE1 );
	glare_buffer->Bind();
	glActiveTexture( GL_TEXTURE2 );
	glBindTexture( GL_TEXTURE_2D, m_lum[1] );
	
	//adaptive exposure adjustment in log space
	
	float newExp = ( float )( exposureCompansation[m_sceneIndex] * log( m_expAdjust + 0.0001 ) );
	
	glUniform1f( m_shaders[TONEMAPPING].auiLocation[0], m_blendAmount );
	glUniform1f( m_shaders[TONEMAPPING].auiLocation[1], newExp );
	glUniform1f( m_shaders[TONEMAPPING].auiLocation[2], m_gamma );
	
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
}

void HDR::blitBuffer( RenderTexture* src )
{
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	glViewport( 0, 0, m_width, m_height );
	glClearColor( 1.0f, 0.0f, 0.0f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	glUseProgram( m_shaders[DOWNSAMPLE].pid );
	glDisable( GL_CULL_FACE );
	glDisable( GL_BLEND );
	glDisable( GL_DEPTH_TEST );
	glActiveTexture( GL_TEXTURE0 );
	src->Bind();
	DrawAxisAlignedQuad( -1, -1, 1, 1 );
	src->Release();
}

void HDR::draw()
{
#if 1
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	glClearColor( m_clearColor.x, m_clearColor.y, m_clearColor.z, m_clearColor.w );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
#endif
	
	glBindVertexArray( m_VAO );
	
	m_transformer->setRotationVel( nv::vec3f( 0.0f, m_autoSpin ? ( NV_PI * 0.05f ) : 0.0f, 0.0f ) );
	m_transformer->update( 1.0F / 60.0F );
	
	// FIXME update();
	updateDynamics();
	render();
}

void HDR::render()
{
	DrawScene();
	
	//Downsize scene buffer for post processing.
	downsample4x( scene_buffer, blur_bufferA[LEVEL_0] );
	
	if( m_autoExposure )
	{
		downsample4x( blur_bufferA[LEVEL_0], exp_buffer[0] );
		downsample4x( exp_buffer[0], exp_buffer[1] );
		calculateLuminance();
	}
	
	//Extract high light area for further processing.
	extractHL( blur_bufferA[LEVEL_0], compose_buffer[LEVEL_0] );
	
	//Gaussian blur on pyramid buffers.
	blur( compose_buffer[LEVEL_0], blur_bufferA[LEVEL_0], blur_bufferB[LEVEL_0], BLURH4 );
	for( int i = LEVEL_1; i < LEVEL_TOTAL; i++ )
	{
		downsample( compose_buffer[i - 1], compose_buffer[i] );
		blur( compose_buffer[i], blur_bufferA[i], blur_bufferB[i], ( BLURH4 + i * 2 ) > BLURH12 ? BLURH12 : ( BLURH4 + i * 2 ) );
	}
	
	//Generate streaks in 4 directions. Generate ghost image in 2 passes.
	if( m_glareType == FILMIC_GLARE )
	{
		genHorizontalGlare( 0 );
		genHorizontalGlare( 1 );
		genGhostImage( filmic_ghost_modulation1st, filmic_ghost_modulation2nd );
	}
	else if( m_glareType == CAMERA_GLARE )
	{
		genStarStreak( 0 );
		genStarStreak( 1 );
		genStarStreak( 2 );
		genStarStreak( 3 );
		genGhostImage( camera_ghost_modulation1st, camera_ghost_modulation2nd );
	}
	
	//Final glare composition.
	ComposeEffect();
	
	//tonemapping to RGB888
	toneMappingPass();
	
	//exchange lunimance texture for next frame
	GLuint temp = m_lum[0];
	m_lum[0] = m_lum[1];
	m_lum[1] = temp;
	
}

void HDR::calculateLuminance()
{
	glUseProgram( m_shaders[CALCLUMINANCE].pid );
	glBindImageTexture( 0, exp_buffer[1]->GetTexId(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F );
	glBindImageTexture( 1, m_lumCurrent, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F );
	glDispatchCompute( 1, 1, 1 );
	//glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	
	glUseProgram( m_shaders[CALCADAPTEDLUM].pid );
	glBindImageTexture( 0, m_lumCurrent, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F );
	glBindImageTexture( 1, m_lum[0], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F );
	glBindImageTexture( 2, m_lum[1], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F );
	
	float time = 1.0F / 60.0F; // FIXME getFrameDeltaTime();
	glUniform1f( m_shaders[CALCADAPTEDLUM].auiLocation[0], time );
	glDispatchCompute( 1, 1, 1 );
	glMemoryBarrier( GL_SHADER_IMAGE_ACCESS_BARRIER_BIT );
}

void HDR::updateDynamics()
{
	nv::perspective( projection_matrix, FOV * 0.5f, m_width / ( float )m_height, Z_NEAR, Z_FAR );
}



static void APIENTRY
DebugCallback( GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
			   const GLchar* message, const void* userParam )
{
	const char* sourceStr = "Source: Unknown";
	const char* typeStr = "Type: Unknown";
	const char* severityStr = "Severity: Unknown";
	
	switch( severity )
	{
		case 0x826B: // GL_DEBUG_SEVERITY_NOTIFICATION_ARB - I don't care
			return;
			
		case GL_DEBUG_SEVERITY_HIGH:
			severityStr = "Severity: High";
			break;
		case GL_DEBUG_SEVERITY_MEDIUM:
			severityStr = "Severity: Medium";
			break;
		case GL_DEBUG_SEVERITY_LOW:
			severityStr = "Severity: Low";
			break;
	}
	
	switch( source )
	{
#define SRCCASE(X)  case GL_DEBUG_SOURCE_ ## X: sourceStr = "Source: " #X; break;
			SRCCASE( API );
			SRCCASE( WINDOW_SYSTEM );
			SRCCASE( SHADER_COMPILER );
			SRCCASE( THIRD_PARTY );
			SRCCASE( APPLICATION );
			SRCCASE( OTHER );
#undef SRCCASE
	}
	
	switch( type )
	{
#define TYPECASE(X)  case GL_DEBUG_TYPE_ ## X: typeStr = "Type: " #X; break;
			TYPECASE( ERROR );
			TYPECASE( DEPRECATED_BEHAVIOR );
			TYPECASE( UNDEFINED_BEHAVIOR );
			TYPECASE( PORTABILITY );
			TYPECASE( PERFORMANCE );
			TYPECASE( OTHER );
#undef TYPECASE
	}
	
	LOGE( "GLDBG %s %s %s: %s", sourceStr, typeStr, severityStr, message );
}

static SDL_Window* window = NULL;
static SDL_GLContext glContext = NULL;

static void cleanupSDL()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	
	if( glContext != NULL )
		SDL_GL_DeleteContext( glContext );
		
	if( window != NULL )
		SDL_DestroyWindow( window );
		
	SDL_Quit();
}

int WINAPI WinMain( HINSTANCE instance, HINSTANCE prevInstance, LPSTR commandLine, int cmdShow )
{
	if( SDL_Init( SDL_INIT_VIDEO ) < 0 )
	{
		LOGE( "SDL_Init() failed: %s\n", SDL_GetError() );
		return 1;
	}
	
	if( SDL_GL_LoadLibrary( NULL ) == -1 )
	{
		LOGE( "SDL_GL_LoadLibrary(NULL) failed: %s\n", SDL_GetError() );
		cleanupSDL();
		return 1;
	}
	
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 4 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 3 );
	
	bool useDebugContext = true;
	
	if( useDebugContext )
	{
		SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG );
	}
	
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
	SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8 );
	
	window = SDL_CreateWindow( "HDR OpenGL 4 Sample", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
							   m_width, m_height, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN );
							   
	if( window == NULL )
	{
		LOGE( "SDL_CreateWindow() failed: %s\n", SDL_GetError() );
		cleanupSDL();
		return 1;
	}
	
	glContext = SDL_GL_CreateContext( window );
	if( glContext == NULL )
	{
		LOGE( "SDL_GL_CreateContext() failed: %s\n", SDL_GetError() );
		cleanupSDL();
		return 1;
	}
	
	// enable vsync
	SDL_GL_SetSwapInterval( 1 );
	
	// init glad OpenGL loader
	if( !gladLoadGLLoader( SDL_GL_GetProcAddress ) )
	{
		LOGE( "ERROR: loading OpenGL function pointers failed!\n" );
		cleanupSDL();
		return 1;
	}
	else if( GLVersion.major < 4 || ( GLVersion.major == 4 && GLVersion.minor < 3 ) )
	{
		LOGE( "ERROR: glad only got GL version %d.%d!\n", GLVersion.major, GLVersion.minor );
		
		cleanupSDL();
		return 1;
	}
	
	if( useDebugContext )
	{
		LOGI( "Installing OpenGL Debug Context Callback\n" );
		
		glDebugMessageCallback( DebugCallback, NULL );
		glEnable( GL_DEBUG_OUTPUT_SYNCHRONOUS );
	}
	
	// Setup Dear ImGui binding
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	( void )io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
	
	// GL 3.0 + GLSL 130
	const char* glsl_version = "#version 130";
	
	ImGui_ImplSDL2_InitForOpenGL( window, glContext );
	ImGui_ImplOpenGL3_Init( glsl_version );
	
	// Setup style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();
	
	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
	// - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
	// - Read 'misc/fonts/README.txt' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	//io.Fonts->AddFontDefault();
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
	//ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
	//IM_ASSERT(font != NULL);
	
	HDR* hdrDemo = new HDR;
	hdrDemo->initRendering();
	
	// Main loop
	bool done = false;
	while( !done )
	{
		// Poll and handle events (inputs, window resize, etc.)
		// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
		// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
		// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
		// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
		SDL_Event event;
		while( SDL_PollEvent( &event ) )
		{
			ImGui_ImplSDL2_ProcessEvent( &event );
			if( event.type == SDL_QUIT )
			{
				done = true;
			}
			
			if( event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID( window ) )
			{
				done = true;
			}
			
			if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE )
			{
				done = true;
			}
		}
		
		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame( window );
		ImGui::NewFrame();
		
		hdrDemo->prepareImGui();
		
		ImGui::Render();
		
		// actually draw to OpenGL
		SDL_GL_MakeCurrent( window, glContext );
		
		hdrDemo->draw();
		
		//glViewport( 0, 0, ( int )io.DisplaySize.x, ( int )io.DisplaySize.y );
		
		// draw ImGui on top
		ImGui_ImplOpenGL3_RenderDrawData( ImGui::GetDrawData() );
		SDL_GL_SwapWindow( window );
	}
	
	cleanupSDL();
	
	return 0;
}