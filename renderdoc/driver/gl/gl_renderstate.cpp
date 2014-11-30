/******************************************************************************
 * The MIT License (MIT)
 * 
 * Copyright (c) 2014 Crytek
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "gl_renderstate.h"
#include "gl_driver.h"

GLRenderState::GLRenderState(const GLHookSet *funcs, Serialiser *ser, LogState state)
	: m_Real(funcs)
	, m_pSerialiser(ser)
	, m_State(state)
{
	Clear();
}

GLRenderState::~GLRenderState()
{
}

void GLRenderState::FetchState()
{
	GLint boolread = 0;
	// TODO check GL_MAX_*
	// TODO check the extensions/core version for these is around
	
	{
		GLenum pnames[] =
		{
			eGL_CLIP_DISTANCE0,
			eGL_CLIP_DISTANCE1,
			eGL_CLIP_DISTANCE2,
			eGL_CLIP_DISTANCE3,
			eGL_CLIP_DISTANCE4,
			eGL_CLIP_DISTANCE5,
			eGL_CLIP_DISTANCE6,
			eGL_CLIP_DISTANCE7,
			eGL_COLOR_LOGIC_OP,
			eGL_CULL_FACE,
			eGL_DEPTH_CLAMP,
			eGL_DEPTH_TEST,
			eGL_DITHER,
			eGL_FRAMEBUFFER_SRGB,
			eGL_LINE_SMOOTH,
			eGL_MULTISAMPLE,
			eGL_POLYGON_SMOOTH,
			eGL_POLYGON_OFFSET_FILL,
			eGL_POLYGON_OFFSET_LINE,
			eGL_POLYGON_OFFSET_POINT,
			eGL_PROGRAM_POINT_SIZE,
			eGL_PRIMITIVE_RESTART,
			eGL_PRIMITIVE_RESTART_FIXED_INDEX,
			eGL_SAMPLE_ALPHA_TO_COVERAGE,
			eGL_SAMPLE_ALPHA_TO_ONE,
			eGL_SAMPLE_COVERAGE,
			eGL_SAMPLE_MASK,
			eGL_STENCIL_TEST,
			eGL_TEXTURE_CUBE_MAP_SEAMLESS,
		};

		RDCCOMPILE_ASSERT(ARRAY_COUNT(pnames) == eEnabled_Count, "Wrong number of pnames");
		
		for(GLuint i=0; i < eEnabled_Count; i++)
			Enabled[i] = (m_Real->glIsEnabled(pnames[i]) == GL_TRUE);
	}

	m_Real->glGetIntegerv(eGL_ACTIVE_TEXTURE, (GLint *)&ActiveTexture);
	
	// TODO fetch bindings for other types than 2D
	for(GLuint i=0; i < (GLuint)ARRAY_COUNT(Tex2D); i++)
	{
		m_Real->glActiveTexture(GLenum(eGL_TEXTURE0 + i));
		m_Real->glGetIntegerv(eGL_TEXTURE_BINDING_2D, (GLint*)&Tex2D[i]);
		m_Real->glGetIntegerv(eGL_SAMPLER_BINDING, (GLint*)&Samplers[i]);
	}
	
	m_Real->glActiveTexture(ActiveTexture);
	
	m_Real->glGetIntegerv(eGL_VERTEX_ARRAY_BINDING, (GLint *)&VAO);
	m_Real->glGetIntegerv(eGL_TRANSFORM_FEEDBACK_BINDING, (GLint *)&FeedbackObj);
	
	// the spec says that you can only query for the format that was previously set, or you get
	// undefined results. Ie. if someone set ints, this might return anything. However there's also
	// no way to query for the type so we just have to hope for the best and hope most people are
	// sane and don't use these except for a default "all 0s" attrib.

	GLuint maxNumAttribs = 0;
	m_Real->glGetIntegerv(eGL_MAX_VERTEX_ATTRIBS, (GLint *)&maxNumAttribs);
	for(GLuint i=0; i < RDCMIN(maxNumAttribs, (GLuint)ARRAY_COUNT(GenericVertexAttribs)); i++)
		m_Real->glGetVertexAttribfv(i, eGL_CURRENT_VERTEX_ATTRIB, &GenericVertexAttribs[i].x);
	
	m_Real->glGetFloatv(eGL_POINT_FADE_THRESHOLD_SIZE, &PointFadeThresholdSize);
	m_Real->glGetIntegerv(eGL_POINT_SPRITE_COORD_ORIGIN, (GLint*)&PointSpriteOrigin);
	m_Real->glGetFloatv(eGL_LINE_WIDTH, &LineWidth);
	m_Real->glGetFloatv(eGL_POINT_SIZE, &PointSize);
	
	m_Real->glGetIntegerv(eGL_PRIMITIVE_RESTART_INDEX, (GLint *)&PrimitiveRestartIndex);
	if(GLCoreVersion >= 45 || ExtensionSupported(ExtensionSupported_ARB_clip_control))
	{
		m_Real->glGetIntegerv(eGL_CLIP_ORIGIN, (GLint *)&ClipOrigin);
		m_Real->glGetIntegerv(eGL_CLIP_DEPTH_MODE, (GLint *)&ClipDepth);
	}
	else
	{
		ClipOrigin = eGL_LOWER_LEFT;
		ClipDepth = eGL_NEGATIVE_ONE_TO_ONE;
	}
	m_Real->glGetIntegerv(eGL_PROVOKING_VERTEX, (GLint *)&ProvokingVertex);

	m_Real->glGetIntegerv(eGL_CURRENT_PROGRAM, (GLint *)&Program);
	m_Real->glGetIntegerv(eGL_PROGRAM_PIPELINE_BINDING, (GLint *)&Pipeline);
	
	GLenum shs[] = {
		eGL_VERTEX_SHADER,
		eGL_TESS_CONTROL_SHADER,
		eGL_TESS_EVALUATION_SHADER,
		eGL_GEOMETRY_SHADER,
		eGL_FRAGMENT_SHADER,
		eGL_COMPUTE_SHADER
	};

	RDCCOMPILE_ASSERT(ARRAY_COUNT(shs) == ARRAY_COUNT(Subroutines), "Subroutine array not the right size");
	for(size_t s=0; s < ARRAY_COUNT(shs); s++)
	{
		GLuint prog = Program;
		if(prog == 0) m_Real->glGetProgramPipelineiv(Pipeline, shs[s], (GLint *)&prog);

		if(prog == 0) continue;

		m_Real->glGetProgramStageiv(prog, shs[s], eGL_ACTIVE_SUBROUTINE_UNIFORM_LOCATIONS, &Subroutines[s].numSubroutines);

		for(GLint i=0; i < Subroutines[s].numSubroutines; i++)
			m_Real->glGetUniformSubroutineuiv(shs[s], i, &Subroutines[s].Values[s]);
	}

	m_Real->glGetIntegerv(eGL_ARRAY_BUFFER_BINDING,              (GLint*)&BufferBindings[eBufIdx_Array]);
	m_Real->glGetIntegerv(eGL_COPY_READ_BUFFER_BINDING,          (GLint*)&BufferBindings[eBufIdx_Copy_Read]);
	m_Real->glGetIntegerv(eGL_COPY_WRITE_BUFFER_BINDING,         (GLint*)&BufferBindings[eBufIdx_Copy_Write]);
	m_Real->glGetIntegerv(eGL_DRAW_INDIRECT_BUFFER_BINDING,      (GLint*)&BufferBindings[eBufIdx_Draw_Indirect]);
	m_Real->glGetIntegerv(eGL_DISPATCH_INDIRECT_BUFFER_BINDING,  (GLint*)&BufferBindings[eBufIdx_Dispatch_Indirect]);
	m_Real->glGetIntegerv(eGL_PIXEL_PACK_BUFFER_BINDING,         (GLint*)&BufferBindings[eBufIdx_Pixel_Pack]);
	m_Real->glGetIntegerv(eGL_PIXEL_UNPACK_BUFFER_BINDING,       (GLint*)&BufferBindings[eBufIdx_Pixel_Unpack]);
	m_Real->glGetIntegerv(eGL_QUERY_BUFFER_BINDING,              (GLint*)&BufferBindings[eBufIdx_Query]);
	m_Real->glGetIntegerv(eGL_TEXTURE_BUFFER_BINDING,            (GLint*)&BufferBindings[eBufIdx_Texture]);

	struct { IdxRangeBuffer *bufs; int count; GLenum binding; GLenum start; GLenum size; GLenum maxcount; } idxBufs[] =
	{
		{ AtomicCounter, ARRAY_COUNT(AtomicCounter), eGL_ATOMIC_COUNTER_BUFFER_BINDING, eGL_ATOMIC_COUNTER_BUFFER_START, eGL_ATOMIC_COUNTER_BUFFER_SIZE, eGL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS, },
		{ ShaderStorage, ARRAY_COUNT(ShaderStorage), eGL_SHADER_STORAGE_BUFFER_BINDING, eGL_SHADER_STORAGE_BUFFER_START, eGL_SHADER_STORAGE_BUFFER_SIZE, eGL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, },
		{ TransformFeedback, ARRAY_COUNT(TransformFeedback), eGL_TRANSFORM_FEEDBACK_BUFFER_BINDING, eGL_TRANSFORM_FEEDBACK_BUFFER_START, eGL_TRANSFORM_FEEDBACK_BUFFER_SIZE, eGL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS, },
		{ UniformBinding, ARRAY_COUNT(UniformBinding), eGL_UNIFORM_BUFFER_BINDING, eGL_UNIFORM_BUFFER_START, eGL_UNIFORM_BUFFER_SIZE, eGL_MAX_UNIFORM_BUFFER_BINDINGS, },
	};

	for(GLuint b=0; b < (GLuint)ARRAY_COUNT(idxBufs); b++)
	{
		GLint maxCount = 0;
		m_Real->glGetIntegerv(idxBufs[b].maxcount, &maxCount);
		for(int i=0; i < idxBufs[b].count && i < maxCount; i++)
		{
			m_Real->glGetIntegeri_v(idxBufs[b].binding, i, (GLint*)&idxBufs[b].bufs[i].name);
			m_Real->glGetInteger64i_v(idxBufs[b].start, i, (GLint64*)&idxBufs[b].bufs[i].start);
			m_Real->glGetInteger64i_v(idxBufs[b].size,  i, (GLint64*)&idxBufs[b].bufs[i].size);
		}
	}
	
	for(GLuint i=0; i < (GLuint)ARRAY_COUNT(Blends); i++)
	{
		m_Real->glGetIntegeri_v(eGL_BLEND_EQUATION_RGB, i, (GLint*)&Blends[i].EquationRGB);
		m_Real->glGetIntegeri_v(eGL_BLEND_EQUATION_ALPHA, i, (GLint*)&Blends[i].EquationAlpha);

		m_Real->glGetIntegeri_v(eGL_BLEND_SRC_RGB, i, (GLint*)&Blends[i].SourceRGB);
		m_Real->glGetIntegeri_v(eGL_BLEND_SRC_ALPHA, i, (GLint*)&Blends[i].SourceAlpha);

		m_Real->glGetIntegeri_v(eGL_BLEND_DST_RGB, i, (GLint*)&Blends[i].DestinationRGB);
		m_Real->glGetIntegeri_v(eGL_BLEND_DST_ALPHA, i, (GLint*)&Blends[i].DestinationAlpha);

		Blends[i].Enabled = (m_Real->glIsEnabledi(eGL_BLEND, i) == GL_TRUE);
	}

	m_Real->glGetFloatv(eGL_BLEND_COLOR, &BlendColor[0]);

	for(GLuint i=0; i < (GLuint)ARRAY_COUNT(Viewports); i++)
		m_Real->glGetFloati_v(eGL_VIEWPORT, i, &Viewports[i].x);
	
	for(GLuint i=0; i < (GLuint)ARRAY_COUNT(Scissors); i++)
	{
		m_Real->glGetIntegeri_v(eGL_SCISSOR_BOX, i, &Scissors[i].x);
		Scissors[i].enabled = (m_Real->glIsEnabledi(eGL_SCISSOR_TEST, i) == GL_TRUE);
	}

	m_Real->glGetIntegerv(eGL_DRAW_FRAMEBUFFER_BINDING, (GLint *)&DrawFBO);
	m_Real->glGetIntegerv(eGL_READ_FRAMEBUFFER_BINDING, (GLint *)&ReadFBO);

	for(size_t i=0; i < ARRAY_COUNT(DrawBuffers); i++)
		m_Real->glGetIntegerv(GLenum(eGL_DRAW_BUFFER0 + i), (GLint *)&DrawBuffers[i]);

	m_Real->glGetIntegerv(eGL_FRAGMENT_SHADER_DERIVATIVE_HINT, (GLint *)&Hints.Derivatives);
	m_Real->glGetIntegerv(eGL_LINE_SMOOTH_HINT, (GLint *)&Hints.LineSmooth);
	m_Real->glGetIntegerv(eGL_POLYGON_SMOOTH_HINT, (GLint *)&Hints.PolySmooth);
	m_Real->glGetIntegerv(eGL_TEXTURE_COMPRESSION_HINT, (GLint *)&Hints.TexCompression);
	
	m_Real->glGetBooleanv(eGL_DEPTH_WRITEMASK, &DepthWriteMask);
	m_Real->glGetFloatv(eGL_DEPTH_CLEAR_VALUE, &DepthClearValue);
	m_Real->glGetIntegerv(eGL_DEPTH_FUNC, (GLint *)&DepthFunc);
	
	for(GLuint i=0; i < (GLuint)ARRAY_COUNT(DepthRanges); i++)
		m_Real->glGetDoublei_v(eGL_DEPTH_RANGE, i, &DepthRanges[i].nearZ);
	
	m_Real->glGetDoublev(eGL_DEPTH_BOUNDS_TEST_EXT, &DepthBounds.nearZ);

	{
		m_Real->glGetIntegerv(eGL_STENCIL_FUNC, (GLint *)&StencilFront.func);
		m_Real->glGetIntegerv(eGL_STENCIL_BACK_FUNC, (GLint *)&StencilBack.func);

		m_Real->glGetIntegerv(eGL_STENCIL_REF, (GLint *)&StencilFront.ref);
		m_Real->glGetIntegerv(eGL_STENCIL_BACK_REF, (GLint *)&StencilBack.ref);

		GLint maskval;
		m_Real->glGetIntegerv(eGL_STENCIL_VALUE_MASK, &maskval); StencilFront.valuemask = uint8_t(maskval&0xff);
		m_Real->glGetIntegerv(eGL_STENCIL_BACK_VALUE_MASK, &maskval); StencilBack.valuemask = uint8_t(maskval&0xff);
		
		m_Real->glGetIntegerv(eGL_STENCIL_WRITEMASK, &maskval); StencilFront.writemask = uint8_t(maskval&0xff);
		m_Real->glGetIntegerv(eGL_STENCIL_BACK_WRITEMASK, &maskval); StencilBack.writemask = uint8_t(maskval&0xff);

		m_Real->glGetIntegerv(eGL_STENCIL_FAIL, (GLint *)&StencilFront.stencilFail);
		m_Real->glGetIntegerv(eGL_STENCIL_BACK_FAIL, (GLint *)&StencilBack.stencilFail);

		m_Real->glGetIntegerv(eGL_STENCIL_PASS_DEPTH_FAIL, (GLint *)&StencilFront.depthFail);
		m_Real->glGetIntegerv(eGL_STENCIL_BACK_PASS_DEPTH_FAIL, (GLint *)&StencilBack.depthFail);

		m_Real->glGetIntegerv(eGL_STENCIL_PASS_DEPTH_PASS, (GLint *)&StencilFront.pass);
		m_Real->glGetIntegerv(eGL_STENCIL_BACK_PASS_DEPTH_PASS, (GLint *)&StencilBack.pass);
	}

	m_Real->glGetIntegerv(eGL_STENCIL_CLEAR_VALUE, (GLint *)&StencilClearValue);
	
	for(size_t i=0; i < ARRAY_COUNT(ColorMasks); i++)
		m_Real->glGetBooleanv(eGL_COLOR_WRITEMASK, &ColorMasks[i].red);

	m_Real->glGetIntegeri_v(eGL_SAMPLE_MASK_VALUE, 0, (GLint *)&SampleMask[0]);
	m_Real->glGetIntegerv(eGL_SAMPLE_COVERAGE_VALUE, (GLint *)&SampleCoverage);
	m_Real->glGetIntegerv(eGL_SAMPLE_COVERAGE_INVERT, (GLint *)&boolread); SampleCoverageInvert = (boolread != 0);
	m_Real->glGetFloatv(eGL_MIN_SAMPLE_SHADING_VALUE, &MinSampleShading);
	
	m_Real->glGetIntegerv(eGL_LOGIC_OP_MODE, (GLint *)&LogicOp);

	m_Real->glGetFloatv(eGL_COLOR_CLEAR_VALUE, &ColorClearValue.red);
	
	m_Real->glGetIntegerv(eGL_PATCH_VERTICES, &PatchParams.numVerts);
	m_Real->glGetFloatv(eGL_PATCH_DEFAULT_INNER_LEVEL, &PatchParams.defaultInnerLevel[0]);
	m_Real->glGetFloatv(eGL_PATCH_DEFAULT_OUTER_LEVEL, &PatchParams.defaultOuterLevel[0]);
	
	{
		// This was listed in docs as enumeration[2] even though polygon mode can't be set independently for front
		// and back faces for a while, so pass large enough array to be sure.
		// AMD driver claims this doesn't exist anymore in core, so don't return any value, set to
		// default GL_FILL to be safe
		GLenum dummy[2] = { eGL_FILL, eGL_FILL };
		m_Real->glGetIntegerv(eGL_POLYGON_MODE, (GLint *)&dummy);
		PolygonMode = dummy[0];
	}
	
	m_Real->glGetFloatv(eGL_POLYGON_OFFSET_FACTOR, &PolygonOffset[0]);
	m_Real->glGetFloatv(eGL_POLYGON_OFFSET_UNITS, &PolygonOffset[1]);

	m_Real->glGetIntegerv(eGL_FRONT_FACE, (GLint *)&FrontFace);
	m_Real->glGetIntegerv(eGL_CULL_FACE_MODE, (GLint *)&CullFace);
}

void GLRenderState::ApplyState()
{
	{
		GLenum pnames[] =
		{
			eGL_CLIP_DISTANCE0,
			eGL_CLIP_DISTANCE1,
			eGL_CLIP_DISTANCE2,
			eGL_CLIP_DISTANCE3,
			eGL_CLIP_DISTANCE4,
			eGL_CLIP_DISTANCE5,
			eGL_CLIP_DISTANCE6,
			eGL_CLIP_DISTANCE7,
			eGL_COLOR_LOGIC_OP,
			eGL_CULL_FACE,
			eGL_DEPTH_CLAMP,
			eGL_DEPTH_TEST,
			eGL_DITHER,
			eGL_FRAMEBUFFER_SRGB,
			eGL_LINE_SMOOTH,
			eGL_MULTISAMPLE,
			eGL_POLYGON_SMOOTH,
			eGL_POLYGON_OFFSET_FILL,
			eGL_POLYGON_OFFSET_LINE,
			eGL_POLYGON_OFFSET_POINT,
			eGL_PROGRAM_POINT_SIZE,
			eGL_PRIMITIVE_RESTART,
			eGL_PRIMITIVE_RESTART_FIXED_INDEX,
			eGL_SAMPLE_ALPHA_TO_COVERAGE,
			eGL_SAMPLE_ALPHA_TO_ONE,
			eGL_SAMPLE_COVERAGE,
			eGL_SAMPLE_MASK,
			eGL_STENCIL_TEST,
			eGL_TEXTURE_CUBE_MAP_SEAMLESS,
		};
		
		RDCCOMPILE_ASSERT(ARRAY_COUNT(pnames) == eEnabled_Count, "Wrong number of pnames");
		
		for(GLuint i=0; i < eEnabled_Count; i++)
			if(Enabled[i]) m_Real->glEnable(pnames[i]); else m_Real->glDisable(pnames[i]);
	}

	for(GLuint i=0; i < (GLuint)ARRAY_COUNT(Tex2D); i++)
	{
		m_Real->glActiveTexture(GLenum(eGL_TEXTURE0 + i));
		m_Real->glBindTexture(eGL_TEXTURE_2D, Tex2D[i]);
		m_Real->glBindSampler(i, Samplers[i]);
	}
	
	m_Real->glActiveTexture(ActiveTexture);

	m_Real->glBindVertexArray(VAO);
	if(FeedbackObj)
		m_Real->glBindTransformFeedback(eGL_TRANSFORM_FEEDBACK, FeedbackObj);
	
	// See FetchState(). The spec says that you have to SET the right format for the shader too,
	// but we couldn't query for the format so we can't set it here.
	GLuint maxNumAttribs = 0;
	m_Real->glGetIntegerv(eGL_MAX_VERTEX_ATTRIBS, (GLint *)&maxNumAttribs);
	for(GLuint i=0; i < RDCMIN(maxNumAttribs, (GLuint)ARRAY_COUNT(GenericVertexAttribs)); i++)
		m_Real->glVertexAttrib4fv(i, &GenericVertexAttribs[i].x);
	
	m_Real->glPointParameterf(eGL_POINT_FADE_THRESHOLD_SIZE, PointFadeThresholdSize);
	m_Real->glPointParameteri(eGL_POINT_SPRITE_COORD_ORIGIN, (GLint)PointSpriteOrigin);
	m_Real->glLineWidth(LineWidth);
	m_Real->glPointSize(PointSize);
	
	m_Real->glPrimitiveRestartIndex(PrimitiveRestartIndex);
	if(m_Real->glClipControl) // only available in 4.5+
		m_Real->glClipControl(ClipOrigin, ClipDepth);
	m_Real->glProvokingVertex(ProvokingVertex);

	m_Real->glUseProgram(Program);
	m_Real->glBindProgramPipeline(Pipeline);
	
	GLenum shs[] = {
		eGL_VERTEX_SHADER,
		eGL_TESS_CONTROL_SHADER,
		eGL_TESS_EVALUATION_SHADER,
		eGL_GEOMETRY_SHADER,
		eGL_FRAGMENT_SHADER,
		eGL_COMPUTE_SHADER
	};

	RDCCOMPILE_ASSERT(ARRAY_COUNT(shs) == ARRAY_COUNT(Subroutines), "Subroutine array not the right size");
	for(size_t s=0; s < ARRAY_COUNT(shs); s++)
		if(Subroutines[s].numSubroutines > 0)
			m_Real->glUniformSubroutinesuiv(shs[s], Subroutines[s].numSubroutines, Subroutines[s].Values);

	m_Real->glBindBuffer(eGL_ARRAY_BUFFER,              BufferBindings[eBufIdx_Array]);
	m_Real->glBindBuffer(eGL_COPY_READ_BUFFER,          BufferBindings[eBufIdx_Copy_Read]);
	m_Real->glBindBuffer(eGL_COPY_WRITE_BUFFER,         BufferBindings[eBufIdx_Copy_Write]);
	m_Real->glBindBuffer(eGL_DRAW_INDIRECT_BUFFER,      BufferBindings[eBufIdx_Draw_Indirect]);
	m_Real->glBindBuffer(eGL_DISPATCH_INDIRECT_BUFFER,  BufferBindings[eBufIdx_Dispatch_Indirect]);
	m_Real->glBindBuffer(eGL_PIXEL_PACK_BUFFER,         BufferBindings[eBufIdx_Pixel_Pack]);
	m_Real->glBindBuffer(eGL_PIXEL_UNPACK_BUFFER,       BufferBindings[eBufIdx_Pixel_Unpack]);
	m_Real->glBindBuffer(eGL_QUERY_BUFFER,              BufferBindings[eBufIdx_Query]);
	m_Real->glBindBuffer(eGL_TEXTURE_BUFFER,            BufferBindings[eBufIdx_Texture]);

	struct { IdxRangeBuffer *bufs; int count; GLenum binding; GLenum maxcount; } idxBufs[] =
	{
		{ AtomicCounter, ARRAY_COUNT(AtomicCounter), eGL_ATOMIC_COUNTER_BUFFER, eGL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS, },
		{ ShaderStorage, ARRAY_COUNT(ShaderStorage), eGL_SHADER_STORAGE_BUFFER, eGL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, },
		{ TransformFeedback, ARRAY_COUNT(TransformFeedback), eGL_TRANSFORM_FEEDBACK_BUFFER, eGL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS, },
		{ UniformBinding, ARRAY_COUNT(UniformBinding), eGL_UNIFORM_BUFFER, eGL_MAX_UNIFORM_BUFFER_BINDINGS, },
	};

	for(size_t b=0; b < ARRAY_COUNT(idxBufs); b++)
	{
		// only restore buffer bindings here if we were using the default transform feedback object
		if(idxBufs[b].binding == eGL_TRANSFORM_FEEDBACK_BUFFER && FeedbackObj) continue;

		GLint maxCount = 0;
		m_Real->glGetIntegerv(idxBufs[b].maxcount, &maxCount);
		for(int i=0; i < idxBufs[b].count && i < maxCount; i++)
		{
			if(idxBufs[b].bufs[i].name == 0 ||
					(idxBufs[b].bufs[i].start == 0 && idxBufs[b].bufs[i].size == 0)
				)
				m_Real->glBindBufferBase(idxBufs[b].binding, i, idxBufs[b].bufs[i].name);
			else
				m_Real->glBindBufferRange(idxBufs[b].binding, i, idxBufs[b].bufs[i].name, (GLintptr)idxBufs[b].bufs[i].start, (GLsizeiptr)idxBufs[b].bufs[i].size);
		}
	}
	
	for(GLuint i=0; i < (GLuint)ARRAY_COUNT(Blends); i++)
	{
		m_Real->glBlendFuncSeparatei(i, Blends[i].SourceRGB, Blends[i].DestinationRGB, Blends[i].SourceAlpha, Blends[i].DestinationAlpha);
		m_Real->glBlendEquationSeparatei(i, Blends[i].EquationRGB, Blends[i].EquationAlpha);
		
		if(Blends[i].Enabled)
			m_Real->glEnablei(eGL_BLEND, i);
		else
			m_Real->glDisablei(eGL_BLEND, i);
	}

	m_Real->glBlendColor(BlendColor[0], BlendColor[1], BlendColor[2], BlendColor[3]);

	m_Real->glViewportArrayv(0, ARRAY_COUNT(Viewports), &Viewports[0].x);

	for (GLuint s = 0; s < (GLuint)ARRAY_COUNT(Scissors); ++s)
	{
		m_Real->glScissorIndexedv(s, &Scissors[s].x);
	
		if (Scissors[s].enabled)
			m_Real->glEnablei(eGL_SCISSOR_TEST, s);
		else
			m_Real->glDisablei(eGL_SCISSOR_TEST, s);
	}

	m_Real->glBindFramebuffer(eGL_READ_FRAMEBUFFER, ReadFBO);
	m_Real->glBindFramebuffer(eGL_DRAW_FRAMEBUFFER, DrawFBO);

	GLenum DBs[8] = { eGL_NONE };
	uint32_t numDBs = 0;
	for(GLuint i=0; i < (GLuint)ARRAY_COUNT(DrawBuffers); i++)
	{
		if(DrawBuffers[i] != eGL_NONE)
		{
			numDBs++;
			DBs[i] = DrawBuffers[i];

			if(m_State < WRITING)
			{
				// since we are faking the default framebuffer with our own
				// to see the results, replace back/front/left/right with color attachment 0
				if(DBs[i] == eGL_BACK_LEFT || DBs[i] == eGL_BACK_RIGHT ||
					DBs[i] == eGL_FRONT_LEFT || DBs[i] == eGL_FRONT_RIGHT)
					DBs[i] = eGL_COLOR_ATTACHMENT0;

				// These aren't valid for glDrawBuffers but can be returned when we call glGet,
				// assume they mean left implicitly
				if(DBs[i] == eGL_BACK ||
					DBs[i] == eGL_FRONT)
					DBs[i] = eGL_COLOR_ATTACHMENT0;
			}
		}
		else
		{
			break;
		}
	}
	m_Real->glDrawBuffers(numDBs, DBs);
	
	m_Real->glHint(eGL_FRAGMENT_SHADER_DERIVATIVE_HINT, Hints.Derivatives);
	m_Real->glHint(eGL_LINE_SMOOTH_HINT, Hints.LineSmooth);
	m_Real->glHint(eGL_POLYGON_SMOOTH_HINT, Hints.PolySmooth);
	m_Real->glHint(eGL_TEXTURE_COMPRESSION_HINT, Hints.TexCompression);
	
	m_Real->glDepthMask(DepthWriteMask);
	m_Real->glClearDepth(DepthClearValue);
	m_Real->glDepthFunc(DepthFunc);
	
	for(GLuint i=0; i < (GLuint)ARRAY_COUNT(DepthRanges); i++)
	{
		double v[2] = { DepthRanges[i].nearZ, DepthRanges[i].farZ };
		m_Real->glDepthRangeArrayv(i, 1, v);
	}

	if(m_Real->glDepthBoundsEXT) // extension, not always available
		m_Real->glDepthBoundsEXT(DepthBounds.nearZ, DepthBounds.farZ);
	
	{
		m_Real->glStencilFuncSeparate(eGL_FRONT, StencilFront.func, StencilFront.ref, StencilFront.valuemask);
		m_Real->glStencilFuncSeparate(eGL_BACK, StencilBack.func, StencilBack.ref, StencilBack.valuemask);

		m_Real->glStencilMaskSeparate(eGL_FRONT, StencilFront.writemask);
		m_Real->glStencilMaskSeparate(eGL_BACK, StencilBack.writemask);

		m_Real->glStencilOpSeparate(eGL_FRONT, StencilFront.stencilFail, StencilFront.depthFail, StencilFront.pass);
		m_Real->glStencilOpSeparate(eGL_BACK, StencilBack.stencilFail, StencilBack.depthFail, StencilBack.pass);
	}

	m_Real->glClearStencil((GLint)StencilClearValue);
	
	for(GLuint i=0; i < (GLuint)ARRAY_COUNT(ColorMasks); i++)
		m_Real->glColorMaski(i, ColorMasks[i].red, ColorMasks[i].green, ColorMasks[i].blue, ColorMasks[i].alpha);

	m_Real->glSampleMaski(0, (GLbitfield)SampleMask[0]);
	m_Real->glSampleCoverage(SampleCoverage, SampleCoverageInvert ? GL_TRUE : GL_FALSE);
	m_Real->glMinSampleShading(MinSampleShading);

	m_Real->glLogicOp(LogicOp);

	m_Real->glClearColor(ColorClearValue.red, ColorClearValue.green, ColorClearValue.blue, ColorClearValue.alpha);
	
	m_Real->glPatchParameteri(eGL_PATCH_VERTICES, PatchParams.numVerts);
	m_Real->glPatchParameterfv(eGL_PATCH_DEFAULT_INNER_LEVEL, PatchParams.defaultInnerLevel);
	m_Real->glPatchParameterfv(eGL_PATCH_DEFAULT_OUTER_LEVEL, PatchParams.defaultOuterLevel);

	m_Real->glPolygonMode(eGL_FRONT_AND_BACK, PolygonMode);
	m_Real->glPolygonOffset(PolygonOffset[0], PolygonOffset[1]);

	m_Real->glFrontFace(FrontFace);
	m_Real->glCullFace(CullFace);
}

void GLRenderState::Clear()
{
	RDCEraseEl(Enabled);

	RDCEraseEl(Tex2D);
	RDCEraseEl(Samplers);
	RDCEraseEl(ActiveTexture);
	
	RDCEraseEl(Program);
	RDCEraseEl(Pipeline);
	
	RDCEraseEl(Subroutines);

	RDCEraseEl(VAO);
	RDCEraseEl(FeedbackObj);
	
	RDCEraseEl(GenericVertexAttribs);
	
	RDCEraseEl(PointFadeThresholdSize);
	RDCEraseEl(PointSpriteOrigin);
	RDCEraseEl(LineWidth);
	RDCEraseEl(PointSize);
	
	RDCEraseEl(PrimitiveRestartIndex);
	RDCEraseEl(ClipOrigin);
	RDCEraseEl(ClipDepth);
	RDCEraseEl(ProvokingVertex);

	RDCEraseEl(BufferBindings);
	RDCEraseEl(AtomicCounter);
	RDCEraseEl(ShaderStorage);
	RDCEraseEl(TransformFeedback);
	RDCEraseEl(UniformBinding);
	RDCEraseEl(Blends);
	RDCEraseEl(BlendColor);
	RDCEraseEl(Viewports);
	RDCEraseEl(Scissors);

	RDCEraseEl(DrawFBO);
	RDCEraseEl(ReadFBO);
	RDCEraseEl(DrawBuffers);

	RDCEraseEl(PatchParams);
	RDCEraseEl(PolygonMode);
	RDCEraseEl(PolygonOffset);
	
	RDCEraseEl(DepthWriteMask);
	RDCEraseEl(DepthClearValue);
	RDCEraseEl(DepthRanges);
	RDCEraseEl(DepthBounds);
	RDCEraseEl(DepthFunc);
	RDCEraseEl(StencilFront);
	RDCEraseEl(StencilBack);
	RDCEraseEl(StencilClearValue);
	RDCEraseEl(ColorMasks);
	RDCEraseEl(SampleMask);
	RDCEraseEl(SampleCoverage);
	RDCEraseEl(SampleCoverageInvert);
	RDCEraseEl(MinSampleShading);
	RDCEraseEl(LogicOp);
	RDCEraseEl(ColorClearValue);

	RDCEraseEl(Hints);
	RDCEraseEl(FrontFace);
	RDCEraseEl(CullFace);
}

void GLRenderState::Serialise(LogState state, void *ctx, WrappedOpenGL *gl)
{
	GLResourceManager *rm = gl->GetResourceManager();
	// TODO check GL_MAX_*

	m_pSerialiser->Serialise<eEnabled_Count>("GL_ENABLED", Enabled);
	
	for(size_t i=0; i < ARRAY_COUNT(Tex2D); i++)
	{
		ResourceId ID = ResourceId();
		if(state >= WRITING) ID = rm->GetID(TextureRes(ctx, Tex2D[i]));
		m_pSerialiser->Serialise("GL_TEXTURE_BINDING_2D", ID);
		if(state < WRITING && ID != ResourceId()) Tex2D[i] = rm->GetLiveResource(ID).name;
	}
	
	for(size_t i=0; i < ARRAY_COUNT(Samplers); i++)
	{
		ResourceId ID = ResourceId();
		if(state >= WRITING) ID = rm->GetID(SamplerRes(ctx, Samplers[i]));
		m_pSerialiser->Serialise("GL_SAMPLER_BINDING", ID);
		if(state < WRITING && ID != ResourceId()) Samplers[i] = rm->GetLiveResource(ID).name;
	}

	m_pSerialiser->Serialise("GL_ACTIVE_TEXTURE", ActiveTexture);
	
	{
		ResourceId ID = ResourceId();
		if(state >= WRITING) ID = rm->GetID(VertexArrayRes(ctx, VAO));
		m_pSerialiser->Serialise("GL_VERTEX_ARRAY_BINDING", ID);
		if(state < WRITING && ID != ResourceId()) VAO = rm->GetLiveResource(ID).name;
	}
	
	{
		ResourceId ID = ResourceId();
		if(state >= WRITING) ID = rm->GetID(FeedbackRes(ctx, FeedbackObj));
		m_pSerialiser->Serialise("GL_TRANSFORM_FEEDBACK_BINDING", ID);
		if(state < WRITING && ID != ResourceId()) FeedbackObj = rm->GetLiveResource(ID).name;
	}
	
	for(size_t i=0; i < ARRAY_COUNT(GenericVertexAttribs); i++)
	{
		m_pSerialiser->Serialise<4>("GL_CURRENT_VERTEX_ATTRIB", &GenericVertexAttribs[i].x);
	}
	
	m_pSerialiser->Serialise("GL_POINT_FADE_THRESHOLD_SIZE", PointFadeThresholdSize);
	m_pSerialiser->Serialise("GL_POINT_SPRITE_COORD_ORIGIN", PointSpriteOrigin);
	m_pSerialiser->Serialise("GL_LINE_WIDTH", LineWidth);
	m_pSerialiser->Serialise("GL_POINT_SIZE", PointSize);
	
	m_pSerialiser->Serialise("GL_PRIMITIVE_RESTART_INDEX", PrimitiveRestartIndex);
	m_pSerialiser->Serialise("GL_CLIP_ORIGIN", ClipOrigin);
	m_pSerialiser->Serialise("GL_CLIP_DEPTH_MODE", ClipDepth);
	m_pSerialiser->Serialise("GL_PROVOKING_VERTEX", ProvokingVertex);

	for(size_t i=0; i < ARRAY_COUNT(BufferBindings); i++)
	{
		ResourceId ID = ResourceId();
		if(state >= WRITING) ID = rm->GetID(BufferRes(ctx, BufferBindings[i]));
		m_pSerialiser->Serialise("GL_BUFFER_BINDING", ID);
		if(state < WRITING && ID != ResourceId()) BufferBindings[i] = rm->GetLiveResource(ID).name;
	}
	
	{
		ResourceId ID = ResourceId();
		if(state >= WRITING) ID = rm->GetID(ProgramRes(ctx, Program));
		m_pSerialiser->Serialise("GL_CURRENT_PROGRAM", ID);
		if(state < WRITING && ID != ResourceId()) Program = rm->GetLiveResource(ID).name;
	}
	{
		ResourceId ID = ResourceId();
		if(state >= WRITING) ID = rm->GetID(ProgramPipeRes(ctx, Pipeline));
		m_pSerialiser->Serialise("GL_PROGRAM_PIPELINE_BINDING", ID);
		if(state < WRITING && ID != ResourceId()) Pipeline = rm->GetLiveResource(ID).name;
	}
	
	for(size_t s=0; s < ARRAY_COUNT(Subroutines); s++)
	{
		m_pSerialiser->Serialise("GL_ACTIVE_SUBROUTINE_UNIFORM_LOCATIONS", Subroutines[s].numSubroutines);
		m_pSerialiser->Serialise<128>("GL_SUBROUTINE_UNIFORMS", Subroutines[s].Values);
	}

	{
		ResourceId ID = ResourceId();
		if(state >= WRITING) ID = rm->GetID(FramebufferRes(ctx, DrawFBO));
		m_pSerialiser->Serialise("GL_DRAW_FRAMEBUFFER_BINDING", ID);
		if(state < WRITING && ID != ResourceId()) DrawFBO = rm->GetLiveResource(ID).name;

		if(DrawFBO == 0) DrawFBO = gl->GetFakeBBFBO();
	}
	{
		ResourceId ID = ResourceId();
		if(state >= WRITING) ID = rm->GetID(FramebufferRes(ctx, ReadFBO));
		m_pSerialiser->Serialise("GL_READ_FRAMEBUFFER_BINDING", ID);
		if(state < WRITING && ID != ResourceId()) ReadFBO = rm->GetLiveResource(ID).name;

		if(ReadFBO == 0) ReadFBO = gl->GetFakeBBFBO();
	}
	
	struct { IdxRangeBuffer *bufs; int count; } idxBufs[] =
	{
		{ AtomicCounter, ARRAY_COUNT(AtomicCounter), },
		{ ShaderStorage, ARRAY_COUNT(ShaderStorage), },
		{ TransformFeedback, ARRAY_COUNT(TransformFeedback), },
		{ UniformBinding, ARRAY_COUNT(UniformBinding), },
	};

	for(size_t b=0; b < ARRAY_COUNT(idxBufs); b++)
	{
		for(int i=0; i < idxBufs[b].count; i++)
		{
			ResourceId ID = ResourceId();
			if(state >= WRITING) ID = rm->GetID(BufferRes(ctx, idxBufs[b].bufs[i].name));
			m_pSerialiser->Serialise("BUFFER_BINDING", ID);
			if(state < WRITING && ID != ResourceId()) idxBufs[b].bufs[i].name = rm->GetLiveResource(ID).name;

			m_pSerialiser->Serialise("BUFFER_START", idxBufs[b].bufs[i].start);
			m_pSerialiser->Serialise("BUFFER_SIZE", idxBufs[b].bufs[i].size);
		}
	}
	
	for(size_t i=0; i < ARRAY_COUNT(Blends); i++)
	{
		m_pSerialiser->Serialise("GL_BLEND_EQUATION_RGB", Blends[i].EquationRGB);
		m_pSerialiser->Serialise("GL_BLEND_EQUATION_ALPHA", Blends[i].EquationAlpha);

		m_pSerialiser->Serialise("GL_BLEND_SRC_RGB", Blends[i].SourceRGB);
		m_pSerialiser->Serialise("GL_BLEND_SRC_ALPHA", Blends[i].SourceAlpha);

		m_pSerialiser->Serialise("GL_BLEND_DST_RGB", Blends[i].DestinationRGB);
		m_pSerialiser->Serialise("GL_BLEND_DST_ALPHA", Blends[i].DestinationAlpha);
		
		m_pSerialiser->Serialise("GL_BLEND", Blends[i].Enabled);
	}
	
	m_pSerialiser->Serialise<4>("GL_BLEND_COLOR", BlendColor);
		
	for(size_t i=0; i < ARRAY_COUNT(Viewports); i++)
	{
		m_pSerialiser->Serialise("GL_VIEWPORT.x", Viewports[i].x);
		m_pSerialiser->Serialise("GL_VIEWPORT.y", Viewports[i].y);
		m_pSerialiser->Serialise("GL_VIEWPORT.w", Viewports[i].width);
		m_pSerialiser->Serialise("GL_VIEWPORT.h", Viewports[i].height);
	}

	for(size_t i=0; i < ARRAY_COUNT(Scissors); i++)
	{
		m_pSerialiser->Serialise("GL_VIEWPORT.x", Scissors[i].x);
		m_pSerialiser->Serialise("GL_VIEWPORT.y", Scissors[i].y);
		m_pSerialiser->Serialise("GL_VIEWPORT.w", Scissors[i].width);
		m_pSerialiser->Serialise("GL_VIEWPORT.h", Scissors[i].height);
	}
	
	m_pSerialiser->Serialise<8>("GL_DRAWBUFFERS", DrawBuffers);

	m_pSerialiser->Serialise("GL_FRAGMENT_SHADER_DERIVATIVE_HINT", Hints.Derivatives);
	m_pSerialiser->Serialise("GL_LINE_SMOOTH_HINT", Hints.LineSmooth);
	m_pSerialiser->Serialise("GL_POLYGON_SMOOTH_HINT", Hints.PolySmooth);
	m_pSerialiser->Serialise("GL_TEXTURE_COMPRESSION_HINT", Hints.TexCompression);
	
	m_pSerialiser->Serialise("GL_DEPTH_WRITEMASK", DepthWriteMask);
	m_pSerialiser->Serialise("GL_DEPTH_CLEAR_VALUE", DepthClearValue);
	m_pSerialiser->Serialise("GL_DEPTH_FUNC", DepthFunc);
	
	for(size_t i=0; i < ARRAY_COUNT(DepthRanges); i++)
	{
		m_pSerialiser->Serialise("GL_DEPTH_RANGE.near", DepthRanges[i].nearZ);
		m_pSerialiser->Serialise("GL_DEPTH_RANGE.far", DepthRanges[i].farZ);
	}
	
	{
		m_pSerialiser->Serialise("GL_DEPTH_BOUNDS_EXT.near", DepthBounds.nearZ);
		m_pSerialiser->Serialise("GL_DEPTH_BOUNDS_EXT.far", DepthBounds.farZ);
	}
	
	{
		m_pSerialiser->Serialise("GL_STENCIL_FUNC", StencilFront.func);
		m_pSerialiser->Serialise("GL_STENCIL_BACK_FUNC", StencilBack.func);

		m_pSerialiser->Serialise("GL_STENCIL_REF", StencilFront.ref);
		m_pSerialiser->Serialise("GL_STENCIL_BACK_REF", StencilBack.ref);

		m_pSerialiser->Serialise("GL_STENCIL_VALUE_MASK", StencilFront.valuemask);
		m_pSerialiser->Serialise("GL_STENCIL_BACK_VALUE_MASK", StencilBack.valuemask);
		
		m_pSerialiser->Serialise("GL_STENCIL_WRITEMASK", StencilFront.writemask);
		m_pSerialiser->Serialise("GL_STENCIL_BACK_WRITEMASK", StencilBack.writemask);

		m_pSerialiser->Serialise("GL_STENCIL_FAIL", StencilFront.stencilFail);
		m_pSerialiser->Serialise("GL_STENCIL_BACK_FAIL", StencilBack.stencilFail);

		m_pSerialiser->Serialise("GL_STENCIL_PASS_DEPTH_FAIL", StencilFront.depthFail);
		m_pSerialiser->Serialise("GL_STENCIL_BACK_PASS_DEPTH_FAIL", StencilBack.depthFail);

		m_pSerialiser->Serialise("GL_STENCIL_PASS_DEPTH_PASS", StencilFront.pass);
		m_pSerialiser->Serialise("GL_STENCIL_BACK_PASS_DEPTH_PASS", StencilBack.pass);
	}

	m_pSerialiser->Serialise("GL_STENCIL_CLEAR_VALUE", StencilClearValue);

	for(size_t i=0; i < ARRAY_COUNT(ColorMasks); i++)
		m_pSerialiser->Serialise<4>("GL_COLOR_WRITEMASK", &ColorMasks[i].red);
	
	m_pSerialiser->Serialise<2>("GL_SAMPLE_MASK_VALUE", &SampleMask[0]);
	m_pSerialiser->Serialise("GL_SAMPLE_COVERAGE_VALUE", SampleCoverage);
	m_pSerialiser->Serialise("GL_SAMPLE_COVERAGE_INVERT", SampleCoverageInvert);
	m_pSerialiser->Serialise("GL_MIN_SAMPLE_SHADING", MinSampleShading);

	m_pSerialiser->Serialise("GL_LOGIC_OP_MODE", LogicOp);

	m_pSerialiser->Serialise<4>("GL_COLOR_CLEAR_VALUE", &ColorClearValue.red);

	{
		m_pSerialiser->Serialise("GL_PATCH_VERTICES", PatchParams.numVerts);
		m_pSerialiser->Serialise<2>("GL_PATCH_DEFAULT_INNER_LEVEL", &PatchParams.defaultInnerLevel[0]);
		m_pSerialiser->Serialise<4>("GL_PATCH_DEFAULT_OUTER_LEVEL", &PatchParams.defaultOuterLevel[0]);
	}

	m_pSerialiser->Serialise("GL_POLYGON_MODE", PolygonMode);
	m_pSerialiser->Serialise("GL_POLYGON_OFFSET_FACTOR", PolygonOffset[0]);
	m_pSerialiser->Serialise("GL_POLYGON_OFFSET_UNITS", PolygonOffset[1]);
		
	m_pSerialiser->Serialise("GL_FRONT_FACE", FrontFace);
	m_pSerialiser->Serialise("GL_CULL_FACE_MODE", CullFace);
}
