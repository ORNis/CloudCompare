//##########################################################################
//#                                                                        #
//#                            CLOUDCOMPARE                                #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU General Public License as published by  #
//#  the Free Software Foundation; version 2 of the License.               #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
//#                                                                        #
//##########################################################################

#include "DxfFilter.h"

//Qt
#include <QApplication>
#include <QFile>

//CCLib
#include <ScalarField.h>

//qCC_db
#include <ccPointCloud.h>
#include <ccPolyline.h>
#include <ccMesh.h>
#include <ccLog.h>
#include <ccNormalVectors.h>

//DXF lib
#ifdef CC_DXF_SUPPORT
#include <dl_dxf.h>
#include <dl_creationadapter.h>
#endif

//system
#include <assert.h>

bool DxfFilter::canLoadExtension(QString upperCaseExt) const
{
	return (upperCaseExt == "DXF");
}

bool DxfFilter::canSave(CC_CLASS_ENUM type, bool& multiple, bool& exclusive) const
{
	if (	type == CC_TYPES::POLY_LINE
		||	type == CC_TYPES::MESH )
	{
		multiple = true;
		exclusive = false;
		return true;
	}
	return false;
}

#ifdef CC_DXF_SUPPORT

//! dxflib-to-CC custom mapper
class DxfImporter : public DL_CreationAdapter
{
public:
	//! Default constructor
	DxfImporter(ccHObject* root, FileIOFilter::LoadParameters& parameters)
		: m_root(root)
		, m_points(0)
		, m_faces(0)
		, m_poly(0)
		, m_polyVertices(0)
		, m_firstPoint(true)
		, m_globalShift(0,0,0)
		, m_loadParameters(parameters)
	{
		assert(m_root);
	}

	CCVector3 convertPoint(double x, double y, double z)
	{
		CCVector3d P(x,y,z);
		if (m_firstPoint)
		{
			if (FileIOFilter::HandleGlobalShift(P,m_globalShift,m_loadParameters))
			{
				ccLog::Warning("[DXF] All points/vertices will been recentered! Translation: (%.2f,%.2f,%.2f)",m_globalShift.x,m_globalShift.y,m_globalShift.z);
			}
			m_firstPoint = false;
		}

		P += m_globalShift;
		return CCVector3::fromArray(P.u);
	}

	void applyGlobalShift()
	{
		if (m_points)
			m_points->setGlobalShift(m_globalShift);
		if (m_polyVertices)
			m_polyVertices->setGlobalShift(m_globalShift);
	}

	virtual void addLayer(const DL_LayerData& data)
	{
		// store our layer colours
		m_layerColourMap[data.name.c_str()] = getAttributes().getColor();
	}

	virtual void addPoint(const DL_PointData& P)
	{
		//create the 'points' point cloud if necessary
		if (!m_points)
		{
			m_points = new ccPointCloud("Points");
			m_root->addChild(m_points);
		}
		if (!m_points->reserve(m_points->size()+1))
		{
			ccLog::Error("[DxfImporter] Not enough memory!");
			return;
		}

		m_points->addPoint(convertPoint(P.x,P.y,P.z));

		ccColor::Rgb col;
		if (getCurrentColour(col))
		{
			//RGB field already instantiated?
			if (m_points->hasColors())
			{
				m_points->addRGBColor(col.rgb);
			}
			//otherwise, reserve memory and set all previous points to white by default
			else if (m_points->setRGBColor(ccColor::white))
			{
				//then replace the last color by the current one
				m_points->setPointColor(m_points->size()-1,col.rgb);
				m_points->showColors(true);
			}
		}
		else if (m_points->hasColors())
		{
			//add default color if none is defined!
			m_points->addRGBColor(ccColor::white.rgba);
		}
	}

	virtual void addPolyline(const DL_PolylineData& poly)
	{
		//create a new polyline if necessary
		if (m_poly && !m_poly->size())
			delete m_poly;
		m_polyVertices = new ccPointCloud("vertices");
		m_poly = new ccPolyline(m_polyVertices);
		m_poly->addChild(m_polyVertices);
		if (!m_polyVertices->reserve(poly.number) || !m_poly->reserve(poly.number))
		{
			ccLog::Error("[DxfImporter] Not enough memory!");
			delete m_poly;
			m_polyVertices = 0;
			m_poly = 0;
			return;
		}
		m_polyVertices->setEnabled(false);
		m_poly->setVisible(true);
		m_poly->setName("Polyline");

		//flags
		m_poly->setClosed(poly.flags & 1);
		//m_poly->set2DMode(poly.flags & 8); //DGM: "2D" polylines in CC doesn't mean the same thing ;)

		//color
		ccColor::Rgb col;
		if (getCurrentColour(col))
		{
			m_poly->setColor(col);
			m_poly->showColors(true);
		}
	}

	virtual void addVertex(const DL_VertexData& vertex)
	{
		//we assume it's a polyline vertex!
		if (	m_poly
			&&	m_polyVertices
			//&&	m_polyVertices->size() < m_polyVertices->capacity()
			)
		{
			if (m_polyVertices->size() == m_polyVertices->capacity())
				m_polyVertices->reserve(m_polyVertices->size()+1);
			
			m_poly->addPointIndex(m_polyVertices->size());
			m_polyVertices->addPoint(convertPoint(vertex.x,vertex.y,vertex.z));

			if (m_poly->size() == 1)
				m_root->addChild(m_poly);
		}
	}

	virtual void add3dFace(const DL_3dFaceData& face)
	{
		//TODO: understand what this really is?!
		CCVector3 P[4];
		for (unsigned i=0; i<4; ++i)
		{
			P[i] = convertPoint(face.x[i],face.y[i],face.z[i]);
		}
		
		//create the 'faces' mesh if necessary
		if (!m_faces)
		{
			ccPointCloud* vertices = new ccPointCloud("vertices");
			m_faces = new ccMesh(vertices);
			m_faces->setName("Faces");
			m_faces->addChild(vertices);
			m_faces->setVisible(true);
			vertices->setEnabled(false);
			vertices->setLocked(true);
			vertices->setGlobalShift(m_globalShift);
			
			m_root->addChild(m_faces);
		}
		
		ccPointCloud* vertices = dynamic_cast<ccPointCloud*>(m_faces->getAssociatedCloud());
		if (!vertices)
		{
			assert(false);
			return;
		}
		
		int vertIndexes[4] = {-1, -1, -1, -1};
		unsigned addedVertCount = 4;
		//check if the two last vertices are the same
		if (P[2].x == P[3].x && P[2].y == P[3].y && P[2].z == P[3].z)
			addedVertCount = 3;

		//current face color
		ccColor::Rgb col;
		ccColor::Rgb* faceCol = 0;
		if (getCurrentColour(col))
			faceCol = &col;

		//look for already defined vertices
		unsigned vertCount = vertices->size();
		if (vertCount)
		{
			//DGM TODO: could we be smarter?
			for (unsigned i=0; i<addedVertCount; ++i)
			{
				for (unsigned j=0; j<vertCount; ++j)
				{
					const CCVector3* Pj = vertices->getPoint(j);
					if (P[i].x == Pj->x && P[i].y == Pj->y && P[i].z == Pj->z)
					{
						bool useCurrentVertex = true;

						//We must also check that the color is the same (if any)
						if (faceCol || vertices->hasColors())
						{
							const ColorCompType* _faceCol = faceCol ? faceCol->rgb : ccColor::white.rgba;
							const ColorCompType* _vertCol = vertices->hasColors() ? vertices->getPointColor(j) : ccColor::white.rgba;
							useCurrentVertex = (_faceCol[0] == _vertCol[0] && _faceCol[1] == _vertCol[1] && _faceCol[2] == _vertCol[2]);
						}

						if (useCurrentVertex)
						{
							vertIndexes[i] = static_cast<int>(j);
							break;
						}
					}
				}
			}
		}

		//now create new vertices
		unsigned createdVertCount = 0;
		{
			for (unsigned i=0; i<addedVertCount; ++i)
				if (vertIndexes[i] < 0)
					++createdVertCount;
		}

		if (createdVertCount != 0)
		{
			//reserve memory for the new vertices
			if (!vertices->reserve(vertCount+createdVertCount))
			{
				ccLog::Error("[DxfImporter] Not enough memory!");
				return;
			}

			for (unsigned i=0; i<addedVertCount; ++i)
			{
				if (vertIndexes[i] < 0)
				{
					vertIndexes[i] = static_cast<int>(vertCount++);
					vertices->addPoint(P[i]);
				}
			}
		}

		//number of triangles to add
		unsigned addTriCount = (addedVertCount == 3 ? 1 : 2);

		//now add the corresponding face(s)
		if (!m_faces->reserve(m_faces->size() + addTriCount))
		{
			ccLog::Error("[DxfImporter] Not enough memory!");
			return;
		}
		m_faces->addTriangle(vertIndexes[0], vertIndexes[1], vertIndexes[2]);
		if (addedVertCount == 4)
			m_faces->addTriangle(vertIndexes[0], vertIndexes[2], vertIndexes[3]);

		//add per-triangle normals
		{
			//normals table
			NormsIndexesTableType* triNormsTable = m_faces->getTriNormsTable();
			bool firstTime = false;
			if (!triNormsTable)
			{
				triNormsTable = new NormsIndexesTableType(); 
				m_faces->setTriNormsTable(triNormsTable);
				firstTime = true;
			}

			//add 1 or 2 new entries
			unsigned triNormCount = triNormsTable->currentSize();
			if (!triNormsTable->reserve(triNormsTable->currentSize() + addTriCount))
			{
				ccLog::Error("[DxfImporter] Not enough memory!");
				return;
			}
			
			CCVector3 N = (P[1]-P[0]).cross(P[2]-P[0]);
			N.normalize();
			triNormsTable->addElement(ccNormalVectors::GetNormIndex(N.u));
			if (addTriCount == 2)
			{
				N = (P[2]-P[0]).cross(P[3]-P[0]);
				N.normalize();
				triNormsTable->addElement(ccNormalVectors::GetNormIndex(N.u));
			}

			//per-triangle normals indexes
			if (firstTime)
			{
				if (!m_faces->reservePerTriangleNormalIndexes())
				{
					ccLog::Error("[DxfImporter] Not enough memory!");
					return;
				}
				m_faces->showNormals(true);
			}
			int n1 = static_cast<int>(triNormCount);
			m_faces->addTriangleNormalIndexes(n1, n1, n1);
			if (addTriCount == 2)
			{
				int n2 = static_cast<int>(triNormCount+1);
				m_faces->addTriangleNormalIndexes(n2, n2, n2);
			}
		}

		//and now for the color
		if (faceCol)
		{
			//RGB field already instantiated?
			if (vertices->hasColors())
			{
				for (unsigned i=0; i<createdVertCount; ++i)
					vertices->addRGBColor(faceCol->rgb);
			}
			//otherwise, reserve memory and set all previous points to white by default
			else if (vertices->setRGBColor(ccColor::white))
			{
				//then replace the last color(s) by the current one
				for (unsigned i=0; i<createdVertCount; ++i)
					vertices->setPointColor(vertCount-1-i,faceCol->rgb);
				m_faces->showColors(true);
			}
		}
		else if (vertices->hasColors())
		{
			//add default color if none is defined!
			for (unsigned i=0; i<createdVertCount; ++i)
				vertices->addRGBColor(ccColor::white.rgba);
		}
	}

	virtual void addLine(const DL_LineData& line)
	{
		//we open lines as simple polylines!
		ccPointCloud* polyVertices = new ccPointCloud("vertices");
		ccPolyline* poly = new ccPolyline(polyVertices);
		poly->addChild(polyVertices);
		if (!polyVertices->reserve(2) || !poly->reserve(2))
		{
			ccLog::Error("[DxfImporter] Not enough memory!");
			delete poly;
			return;
		}
		polyVertices->setEnabled(false);
		poly->setVisible(true);
		poly->setName("Line");
		poly->addPointIndex(0,2);
		//add first point
		polyVertices->addPoint(convertPoint(line.x1,line.y1,line.z1));
		//add second point
		polyVertices->addPoint(convertPoint(line.x2,line.y2,line.z2));
		polyVertices->setGlobalShift(m_globalShift);

		//flags
		poly->setClosed(false);

		//color
		ccColor::Rgb col;
		if (getCurrentColour(col))
		{
			poly->setColor(col);
			poly->showColors(true);
		}

		m_root->addChild(poly);
	}

protected:

	//! Root object (new objects will be added as its children)
	ccHObject* m_root;

	//! Points
	ccPointCloud* m_points;
	//! Faces
	ccMesh* m_faces;
	//! Current polyline (if any)
	ccPolyline* m_poly;
	//! Current polyline vertices
	ccPointCloud* m_polyVertices;

private:

	//! Returns current colour (either the current data's colour or the current layer's one)
	bool getCurrentColour( ccColor::Rgb& ccColour )
	{
		const DL_Attributes attributes = getAttributes();

		int colourIndex = attributes.getColor();

		if ( colourIndex == 0 )
		{
			// TODO Colours BYBLOCK not handled
			return false;
		}
		else if ( colourIndex == 256 )
		{
			// an attribute of 256 means the colours are BYLAYER, so grab it from our map instead
			const int defaultIndex = -1;
			colourIndex = m_layerColourMap.value( attributes.getLayer().c_str(), defaultIndex );

			//if we don't have any information on the current layer
			if (colourIndex == defaultIndex)
				return false;
		}

		ccColour.r = static_cast<ColorCompType>( dxfColors[colourIndex][0] * ccColor::MAX );
		ccColour.g = static_cast<ColorCompType>( dxfColors[colourIndex][1] * ccColor::MAX );
		ccColour.b = static_cast<ColorCompType>( dxfColors[colourIndex][2] * ccColor::MAX );

		return true;
	}

	//! Keep track of the colour of each layer in case the colour attribute is set to BYLAYER
	QMap<QString,int> m_layerColourMap;

	//! Whether the very first point has been loaded or not
	bool m_firstPoint;

	//! Global shift
	CCVector3d m_globalShift;

	//! Load parameters
	FileIOFilter::LoadParameters m_loadParameters;
};

#endif //CC_DXF_SUPPORT

CC_FILE_ERROR DxfFilter::saveToFile(ccHObject* root, QString filename, SaveParameters& parameters)
{
#ifndef CC_DXF_SUPPORT

	ccLog::Error("[DXF] DXF format not supported! Check compilation parameters!");
	return CC_FERR_CONSOLE_ERROR;

#else

	if (!root || filename.isEmpty())
		return CC_FERR_BAD_ARGUMENT;

	ccHObject::Container polylines;
	root->filterChildren(polylines,true,CC_TYPES::POLY_LINE);
	if (root->isKindOf(CC_TYPES::POLY_LINE))
		polylines.push_back(root);
	ccHObject::Container meshes;
	root->filterChildren(meshes,true,CC_TYPES::MESH);
	if (root->isKindOf(CC_TYPES::MESH))
		meshes.push_back(root);

	//only polylines are handled for now
	size_t polyCount = polylines.size();
	size_t meshCount = meshes.size();
	if (polyCount + meshCount == 0)
		return CC_FERR_NO_SAVE;

	//get global bounding box
	CCVector3d bbMinCorner, bbMaxCorner;
	{
		bool firstEntity = true;
		for (size_t i=0; i<polyCount; ++i)
		{
			CCVector3d minC, maxC;
			if (polylines[i]->getGlobalBB(minC,maxC))
			{
				//update global BB
				if (firstEntity)
				{
					bbMinCorner = minC;
					bbMaxCorner = maxC;
					firstEntity = false;
				}
				else
				{
					bbMinCorner.x = std::min(bbMinCorner.x,minC.x);
					bbMinCorner.y = std::min(bbMinCorner.y,minC.y);
					bbMinCorner.z = std::min(bbMinCorner.z,minC.z);
					bbMaxCorner.x = std::max(bbMaxCorner.x,maxC.x);
					bbMaxCorner.y = std::max(bbMaxCorner.y,maxC.y);
					bbMaxCorner.z = std::max(bbMaxCorner.z,maxC.z);
				}
			}
		}
		for (size_t j=0; j<meshCount; ++j)
		{
			CCVector3d minC, maxC;
			if (meshes[j]->getGlobalBB(minC,maxC))
			{
				//update global BB
				if (firstEntity)
				{
					bbMinCorner = minC;
					bbMaxCorner = maxC;
					firstEntity = false;
				}
				else
				{
					bbMinCorner.x = std::min(bbMinCorner.x,minC.x);
					bbMinCorner.y = std::min(bbMinCorner.y,minC.y);
					bbMinCorner.z = std::min(bbMinCorner.z,minC.z);
					bbMaxCorner.x = std::max(bbMaxCorner.x,maxC.x);
					bbMaxCorner.y = std::max(bbMaxCorner.y,maxC.y);
					bbMaxCorner.z = std::max(bbMaxCorner.z,maxC.z);
				}
			}
		}
	}

	CCVector3d diag = bbMaxCorner - bbMinCorner;
	double baseSize = std::max(diag.x,diag.y);
	double lineWidth = baseSize / 40.0;
	double pageMargin = baseSize / 20.0;

	DL_Dxf dxf;
	DL_WriterA* dw = dxf.out(qPrintable(filename), DL_VERSION_R12);
	if (!dw)
	{
		return CC_FERR_WRITING;
	}

	CC_FILE_ERROR result = CC_FERR_NO_ERROR;

	try
	{
		//write header
		dxf.writeHeader(*dw);

		//add dimensions
		dw->dxfString(9, "$INSBASE");
		dw->dxfReal(10,0.0);
		dw->dxfReal(20,0.0);
		dw->dxfReal(30,0.0);
		dw->dxfString(9, "$EXTMIN");
		dw->dxfReal(10,bbMinCorner.x-pageMargin);
		dw->dxfReal(20,bbMinCorner.y-pageMargin);
		dw->dxfReal(30,bbMinCorner.z-pageMargin);
		dw->dxfString(9, "$EXTMAX");
		dw->dxfReal(10,bbMaxCorner.x+pageMargin);
		dw->dxfReal(20,bbMaxCorner.y+pageMargin);
		dw->dxfReal(30,bbMaxCorner.z+pageMargin);
		dw->dxfString(9, "$LIMMIN");
		dw->dxfReal(10,bbMinCorner.x-pageMargin);
		dw->dxfReal(20,bbMinCorner.y-pageMargin);
		dw->dxfString(9, "$LIMMAX");
		dw->dxfReal(10,bbMaxCorner.x+pageMargin);
		dw->dxfReal(20,bbMaxCorner.y+pageMargin);

		//close header
		dw->sectionEnd();

		//Opening the Tables Section
		dw->sectionTables();
		//Writing the Viewports
		dxf.writeVPort(*dw);

		//Writing the Linetypes (all by default)
		{
			dw->tableLineTypes(25);
			dxf.writeLineType(*dw, DL_LineTypeData("BYBLOCK", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("BYLAYER", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("CONTINUOUS", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("ACAD_ISO02W100", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("ACAD_ISO03W100", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("ACAD_ISO04W100", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("ACAD_ISO05W100", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("BORDER", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("BORDER2", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("BORDERX2", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("CENTER", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("CENTER2", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("CENTERX2", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("DASHDOT", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("DASHDOT2", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("DASHDOTX2", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("DASHED", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("DASHED2", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("DASHEDX2", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("DIVIDE", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("DIVIDE2", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("DIVIDEX2", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("DOT", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("DOT2", 0));
			dxf.writeLineType(*dw, DL_LineTypeData("DOTX2", 0));
			dw->tableEnd();
		}

		//Writing the Layers
		dw->tableLayers(static_cast<int>(polyCount)+1);
		QStringList polyLayerNames;
		QStringList meshLayerNames;
		{
			//default layer
			dxf.writeLayer(*dw, 
				DL_LayerData("0", 0), 
				DL_Attributes(
				std::string(""),		// leave empty
				DL_Codes::black,		// default color
				100,					// default width (in 1/100 mm)
				"CONTINUOUS"));			// default line style

			//polylines layers
			for (unsigned i=0; i<polyCount; ++i)
			{
				//default layer name
				//TODO: would be better to use the polyline name!
				//but it can't be longer than 31 characters (R14 limit)
				QString layerName = QString("POLYLINE_%1").arg(i+1,3,10,QChar('0'));

				polyLayerNames << layerName;
				dxf.writeLayer(*dw, 
					DL_LayerData(qPrintable(layerName), 0), //DGM: warning, toStdString doesn't preserve "local" characters
					DL_Attributes(
					std::string(""),
					DL_Codes::green,
					static_cast<int>(lineWidth),
					"CONTINUOUS"));
			}
		
			//mesh layers
			for (unsigned j=0; j<meshCount; ++j)
			{
				//default layer name
				//TODO: would be better to use the mesh name!
				//but it can't be longer than 31 characters (R14 limit)
				QString layerName = QString("MESH_%1").arg(j+1,3,10,QChar('0'));

				meshLayerNames << layerName;
				dxf.writeLayer(*dw, 
					DL_LayerData(qPrintable(layerName), 0), //DGM: warning, toStdString doesn't preserve "local" characters
					DL_Attributes(
					std::string(""),
					DL_Codes::magenta,
					static_cast<int>(lineWidth),
					"CONTINUOUS"));
			}
		}
		dw->tableEnd();

		//Writing Various Other Tables
		//dxf.writeStyle(*dw); //DXFLIB V2.5
		dxf.writeStyle(*dw,DL_StyleData("Standard",0,0.0,0.75,0.0,0,2.5,"txt","")); //DXFLIB V3.3
		dxf.writeView(*dw);
		dxf.writeUcs(*dw);

		dw->tableAppid(1);
		dw->tableAppidEntry(0x12);
		dw->dxfString(2, "ACAD");
		dw->dxfInt(70, 0);
		dw->tableEnd();

		//Writing Dimension Styles
		dxf.writeDimStyle(	*dw, 
							/*arrowSize*/1, 
							/*extensionLineExtension*/1,
							/*extensionLineOffset*/1,
							/*dimensionGap*/1,
							/*dimensionTextSize*/1);
	
		//Writing Block Records
		dxf.writeBlockRecord(*dw);
		dw->tableEnd();

		//Ending the Tables Section
		dw->sectionEnd();

		//Writing the Blocks Section
		{
			dw->sectionBlocks();

			dxf.writeBlock(*dw,  DL_BlockData("*Model_Space", 0, 0.0, 0.0, 0.0));
			dxf.writeEndBlock(*dw, "*Model_Space");

			dxf.writeBlock(*dw, DL_BlockData("*Paper_Space", 0, 0.0, 0.0, 0.0));
			dxf.writeEndBlock(*dw, "*Paper_Space");

			dxf.writeBlock(*dw, DL_BlockData("*Paper_Space0", 0, 0.0, 0.0, 0.0));
			dxf.writeEndBlock(*dw, "*Paper_Space0");

			dw->sectionEnd();
		}

		//Writing the Entities Section
		{
			dw->sectionEntities();

			//write polylines
			for (unsigned i=0; i<polyCount; ++i)
			{
				const ccPolyline* poly = static_cast<ccPolyline*>(polylines[i]);
				unsigned vertexCount = poly->size();
				int flags = poly->isClosed() ? 1 : 0;
				if (!poly->is2DMode())
					flags |= 8; //3D polyline
				dxf.writePolyline(	*dw,
									DL_PolylineData(static_cast<int>(vertexCount),0,0,flags),
									DL_Attributes(qPrintable(polyLayerNames[i]), DL_Codes::bylayer, -1, "BYLAYER") ); //DGM: warning, toStdString doesn't preserve "local" characters

				for (unsigned v=0; v<vertexCount; ++v)
				{
					CCVector3 Pl;
					poly->getPoint(v,Pl);
					CCVector3d P = poly->toGlobal3d(Pl);
					dxf.writeVertex(*dw, DL_VertexData(	P.x, P.y, P.z ) );
				}

				dxf.writePolylineEnd(*dw);
			}

			//write meshes
			for (unsigned j=0; j<meshCount; ++j)
			{
				ccGenericMesh* mesh = static_cast<ccGenericMesh*>(meshes[j]);
				ccGenericPointCloud* vertices = mesh->getAssociatedCloud();
				assert(vertices);
				
				unsigned triCount = mesh->size();
				mesh->placeIteratorAtBegining();
				for (unsigned f=0; f<triCount; ++f)
				{
					const CCLib::GenericTriangle* tri = mesh->_getNextTriangle();
					CCVector3d A = vertices->toGlobal3d(*tri->_getA());
					CCVector3d B = vertices->toGlobal3d(*tri->_getB());
					CCVector3d C = vertices->toGlobal3d(*tri->_getC());
					dxf.write3dFace(*dw,
									DL_3dFaceData(	A.x,A.y,A.z,
													B.x,B.y,B.z,
													C.x,C.y,C.z,
													C.x,C.y,C.z,
													lineWidth ),
									DL_Attributes(qPrintable(meshLayerNames[j]), DL_Codes::bylayer, -1, "BYLAYER")); //DGM: warning, toStdString doesn't preserve "local" characters
				}
			}

			dw->sectionEnd();
		}

		//Writing the Objects Section
		dxf.writeObjects(*dw);
		dxf.writeObjectsEnd(*dw);

		//Ending and Closing the File
		dw->dxfEOF();
		dw->close();
	}
	catch(...)
	{
		ccLog::Warning("[DXF] DxfLib has thrown an unknown exception!");
		result = CC_FERR_THIRD_PARTY_LIB_EXCEPTION;
	}

	delete dw;
	dw = 0;

	return result;

#endif
}

CC_FILE_ERROR DxfFilter::loadFile(QString filename, ccHObject& container, LoadParameters& parameters)
{
	CC_FILE_ERROR result = CC_FERR_NO_LOAD;

#ifdef CC_DXF_SUPPORT
	try
	{
		DxfImporter importer(&container,parameters);
		if (DL_Dxf().in(qPrintable(filename), &importer))
		{
			importer.applyGlobalShift(); //apply the (potential) global shift to shared clouds
			if (container.getChildrenNumber() != 0)
				result = CC_FERR_NO_ERROR;
		}
		else
		{
			result = CC_FERR_READING;
		}
	}
	catch(...)
	{
		ccLog::Warning("[DXF] DxfLib has thrown an unknown exception!");
		result = CC_FERR_THIRD_PARTY_LIB_EXCEPTION;
	}
#else
	
	ccLog::Error("[DXF] Not supported in this version!");

#endif

	return result;
}
