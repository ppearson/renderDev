/*
 ImagineRender: an integration of Imagine into Nuke as a Render node
 Copyright 2012-2016 Peter Pearson.
*/

#include "imagine_render.h"

#include "DDImage/CameraOp.h"
#include "DDImage/LightOp.h"
#include "DDImage/GeoOp.h"
#include "DDImage/Format.h"
#include "DDImage/Scene.h"
#include "DDImage/ImagePlane.h"

// imagine
#include "objects/camera.h"
#include "objects/mesh.h"
#include "lights/point_light.h"
#include "geometry/standard_geometry_instance.h"
#include "geometry/standard_geometry_operations.h"
#include "materials/standard_material.h"
#include "image/image_colour3f.h"
#include "image/image_1f.h"
#include "textures/image/image_texture_3f.h"
#include "textures/image/image_texture_1f.h"

using namespace DD::Image;

void ImagineRenderIop::buildScene()
{
	Op* pInput0 = Op::input(0);
	Op* pInput1 = Op::input(1);

	if (dynamic_cast<CameraOp*>(pInput0))
	{
		CameraOp* pCamera = (CameraOp*)pInput0;

		m_scene.freeCamera();

		DD::Image::Knob* pTrans = pCamera->knob("translate");
		DD::Image::Knob* pRotate = pCamera->knob("rotate");

		Vector position(pTrans->get_value(0), pTrans->get_value(1), pTrans->get_value(2));
		Vector rotation(pRotate->get_value(0), pRotate->get_value(1), pRotate->get_value(2));

		// Because we're currently using Imagine in Embedded mode, we have to use baked 4x4 transforms
		::Matrix4 transform;
		transform.translate(position);
		transform.rotate(rotation, ::Matrix4::eYXZ);

		// TODO: get 4x4 transform as well and slap that on the end

		Camera* pImagineCamera = new Camera();
		pImagineCamera->transform().setCachedTransform(transform);

		float focalLength = pCamera->focal_length();
		float horizAperture = pCamera->film_width();

		// work out the actual FOV (assuming perspective for the moment)

		float fov = 2.0f * std::atan((horizAperture / 2.0f) / focalLength);
		fov = ::degreesf(fov);

		pImagineCamera->setFOV(fov);

		if (m_projectionType == eSpherical)
		{
			pImagineCamera->setProjectionType(Camera::eSpherical);
		}
		else if (m_projectionType == eFisheye)
		{
			pImagineCamera->setProjectionType(Camera::eFisheye);
		}

		m_scene.setDefaultCamera(pImagineCamera);
	}

	m_scene.freeLights();
	DD::Image::Hash geometryHash = -1;

	if (dynamic_cast<GeoOp*>(pInput1))
	{
		GeoOp* pGeo = (GeoOp*)pInput1;

		/// construct Scene

		DD::Image::Scene nukeScene;
		pGeo->build_scene(nukeScene);

		DD::Image::Scene scene2;
		GeometryList geoList;
		pGeo->get_geometry(scene2, geoList);

		if (geoList.size() == 0)
			return;

		geometryHash.append(geoList.size());
		geometryHash.append(pGeo->hash(DD::Image::Group_Points));
	}

	if (geometryHash != m_geometryHash)
	{
		// the scene's changed, so update the geo
		// TODO: this doesn't work, and doesn't handle textures anyway, so we need a more fine-grained way of doing this
	}

	// for the moment, just rebuild everything again...
	m_scene.freeGeometry();

	if (dynamic_cast<GeoOp*>(pInput1))
	{
		GeoOp* pGeo = (GeoOp*)pInput1;

		DD::Image::Scene nukeScene;
		pGeo->build_scene(nukeScene);

		std::vector<LightContext*>::iterator itLight = nukeScene.lights.begin();
		for (; itLight != nukeScene.lights.end(); ++itLight)
		{
			LightContext* pLightC = *itLight;

			LightOp* pLight = pLightC->light();

			DD::Image::Knob* pTrans = pLight->knob("translate");
			DD::Image::Knob* pRotate = pLight->knob("rotate");

			Point position(pTrans->get_value(0), pTrans->get_value(1), pTrans->get_value(2));
			Vector rotation(pRotate->get_value(0), pRotate->get_value(1), pRotate->get_value(2));

			Light* pNewLight = NULL;
			bool spot = false;
			DD::Image::Knob* pType = pLight->knob("light_type");
			if (pType)
			{
				float lightType = pType->get_value(0);
				if (lightType == 2.0f)
					spot = true;
			}

			pNewLight = new PointLight();

			pNewLight->setPosition(position);
			pNewLight->setRotation(rotation);

			DD::Image::Knob* pIntensity = pLight->knob("intensity");
			float intensity = pIntensity->get_value();

			pNewLight->setIntensity(intensity / 4);

			m_scene.addObject(pNewLight, false, false);
		}

		DD::Image::Scene scene2;
		GeometryList geoList;
		pGeo->get_geometry(scene2, geoList);

		if (geoList.size() == 0)
			return;

		StandardMaterial* pNewMat = NULL;

		for (unsigned int objIndex = 0; objIndex < geoList.size(); objIndex++)
		{
			DD::Image::GeoInfo& object = geoList.object(objIndex);

			bool haveUVs = false;

			const AttribContext* pUVs = object.get_attribcontext("uv");
			// check we've got valid UVs
			if (pUVs && pUVs->attribute && pUVs->attribute->size())
				haveUVs = true;

			if (object.material)
			{
				// create a new material type which reads from an Iop and brute-force reads
				// the full texture ahead of time...

				Iop* pNukeMaterial = object.material;

				pNukeMaterial->request(Mask_RGBA, 1);

				bool haveTexture = true;

				// see if we've got a default material
				float red = pNukeMaterial->at(0, 0, Chan_Red);
				float green = pNukeMaterial->at(0, 0, Chan_Green);
				float blue = pNukeMaterial->at(0, 0, Chan_Blue);
				float alpha = pNukeMaterial->at(0, 0, Chan_Alpha);

				// this obviously doesn't work reliably with realistic textures and alpha
/*				if (red == 0.0f && green == 0.0f && blue == 0.0f)
				{
					haveTexture = false;
				}
*/
				if (haveUVs && haveTexture)
				{
					unsigned int width = pNukeMaterial->info().w();
					unsigned int height = pNukeMaterial->info().h();

					// this is going to be a bit stupid, as it won't detect constant alpha
					bool haveAlpha = pNukeMaterial->info().channels().contains(Chan_Alpha);

					DD::Image::Box bbox(0, 0, width, height);

					DD::Image::ImagePlane newImagePlane(bbox, false, haveAlpha ? Mask_RGBA : Mask_RGB, haveAlpha ? 4 : 3);
					// make a texture
					pNukeMaterial->fetchPlane(newImagePlane);
					const float* pRawValues = newImagePlane.readable();

					ImageColour3f* pNewColourImage = new ImageColour3f(width, height);
					Image1f* pNewAlphaImage = NULL;
					if (haveAlpha)
					{
						pNewAlphaImage = new Image1f(width, height);
					}

					for (unsigned int y = 0; y < height; y++)
					{
						for (unsigned int x = 0; x < width; x++)
						{
							float red = *pRawValues++;
							float green = *pRawValues++;
							float blue = *pRawValues++;

							if (haveAlpha)
							{
								float alpha = *pRawValues++;

								pNewAlphaImage->floatAt(x, y) = alpha;
							}

							Colour3f& imagePixel = pNewColourImage->colourAt(x, y);

							imagePixel.r = red;
							imagePixel.g = green;
							imagePixel.b = blue;
						}
					}

					pNewMat = new StandardMaterial();

					if (pNewColourImage)
					{
						ImageTexture3f* pImageTexture = new ImageTexture3f(pNewColourImage, width, height);
						pNewMat->setDiffuseColourManualTexture(pImageTexture);
					}

					if (haveAlpha && pNewAlphaImage)
					{
						ImageTexture1f* pAlphaTexture = new ImageTexture1f(pNewAlphaImage, width, height);
						pNewMat->setAlphaManualTexture(pAlphaTexture);
					}

				}
				else
				{
					float alpha = pNukeMaterial->at(0, 0, Chan_Alpha);

					// hack to check for null input - Nuke's creating a default black Iop when there isn't one...

					if (red == 0.0f && green == 0.0f && blue == 0.0f && alpha == 0.0f)
					{
						// there's a black material attached - possibly the default, so there isn't really a
						// material attached...
						// there's no colour - for the moment set a default so it's obvious what's going on.
						pNewMat = new StandardMaterial();
						pNewMat->setDiffuseColour(Colour3f(0.6f, 0.6f, 0.0f));
					}
					else
					{
						pNewMat = new StandardMaterial();
						pNewMat->setDiffuseColour(Colour3f(red, green, blue));
					}
				}
			}
			else
			{
				// this never actually seems to trigger as Nuke's geo always seems to
				// have materials...
				pNewMat = new StandardMaterial();
				pNewMat->setDiffuseColour(Colour3f(0.6f, 0.6f, 0.6f));
			}

			Material* pMat = static_cast<Material*>(pNewMat);

			StandardGeometryInstance* pNewGeoInstance = new StandardGeometryInstance();

			std::vector<Point>& points = pNewGeoInstance->getPoints();

			const PointList* geomPoints = object.point_list();
			unsigned int numPoints = geomPoints->size();

			for (unsigned int pointIndex = 0; pointIndex < numPoints; pointIndex++)
			{
				const Vector3& localPoint = (*geomPoints)[pointIndex];
				points.push_back(Point(localPoint.x, localPoint.y, localPoint.z));
			}

			// check we've got valid UVs
			if (haveUVs)
			{
				int uvGroupType = pUVs->group;
				unsigned int numUVs = pUVs->attribute->size();

				std::vector<UV>& uvs = pNewGeoInstance->getUVs();

				for (unsigned int uvIndex = 0; uvIndex < numUVs; uvIndex++)
				{
					DD::Image::Vector4& sourceUV = pUVs->attribute->vector4(uvIndex);
					UV newUV(sourceUV.x, sourceUV.y);

					uvs.push_back(newUV);
				}
			}

			unsigned int numFaces = object.primitives();

			std::vector<uint32_t>& aPolyOffsets = pNewGeoInstance->getPolygonOffsets();
			aPolyOffsets.reserve(numFaces);

			std::vector<uint32_t>& aPolyIndices = pNewGeoInstance->getPolygonIndices();

			unsigned int polyOffsetLast = 0;

			for (unsigned int faceIndex = 0; faceIndex < numFaces; faceIndex++)
			{
				const DD::Image::Primitive* prim = object.primitive(faceIndex);
				unsigned int numVertices = prim->vertices();

				unsigned int numSubFaces = prim->faces();
				if (numSubFaces == 1)
				{
					aPolyOffsets.push_back(numVertices + polyOffsetLast);

					for (unsigned int vertexIndex = 0; vertexIndex < numVertices; vertexIndex++)
					{
						unsigned int thisVertexIndex = prim->vertex(vertexIndex);
						aPolyIndices.push_back(thisVertexIndex);
					}

					polyOffsetLast += numVertices;
				}
				else
				{
					for (unsigned int subFace = 0; subFace < numSubFaces; subFace++)
					{
						unsigned int numSubFaceVertices = prim->face_vertices(subFace);
						unsigned int* pSubFaceVertices = new unsigned int[numSubFaceVertices];
						prim->get_face_vertices(subFace, pSubFaceVertices);

						aPolyOffsets.push_back(numSubFaceVertices + polyOffsetLast);

						for (unsigned int vertexIndex = 0; vertexIndex < numSubFaceVertices; vertexIndex++)
						{
							unsigned int thisVertexIndex = pSubFaceVertices[numSubFaceVertices - 1 - vertexIndex];

							aPolyIndices.push_back(thisVertexIndex);
						}

						polyOffsetLast += numSubFaceVertices;

						if (pSubFaceVertices)
							delete pSubFaceVertices;
					}
				}
			}

			if (haveUVs)
			{
				// for the moment, we can cheat here and pass in the point indices as UV indices
				// which works for simple shapes, but obviously need to be changed at some point...
				pNewGeoInstance->setUVIndices(aPolyIndices);
				pNewGeoInstance->setHasPerVertexUVs(true);
			}

			Mesh* pMesh = new Mesh();

			::Matrix4 objectTransform;
			for (unsigned int i = 0; i < 4; i++)
			{
				for (unsigned int j = 0; j < 4; j++)
				{
					objectTransform.at(i, j) = object.matrix[j][i];
				}
			}

			pMesh->transform().setCachedTransform(objectTransform);

			pMesh->setGeometryInstance(pNewGeoInstance);
			pMesh->setMaterial(pMat);

			m_scene.addObject(pMesh, false, false);
		}

		m_geometryHash = geometryHash;
	}
}
