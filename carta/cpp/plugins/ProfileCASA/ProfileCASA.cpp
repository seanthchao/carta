#include "ProfileCASA.h"
#include "plugins/CasaImageLoader/CCImage.h"
#include "plugins/CasaImageLoader/CCMetaDataInterface.h"
#include "CartaLib/Hooks/Initialize.h"
#include "CartaLib/Hooks/ProfileHook.h"
#include "CartaLib/ProfileInfo.h"
#include "CartaLib/Regions/IRegion.h"
#include "CartaLib/IImage.h"
#include <coordinates/Coordinates/DirectionCoordinate.h>
#include <images/Regions/WCEllipsoid.h>
#include <images/Regions/RegionManager.h>

#include <iterator>

using namespace std;
#include <imageanalysis/ImageAnalysis/PixelValueManipulator.h>
#include <imageanalysis/ImageAnalysis/PixelValueManipulatorData.h>
#include <imageanalysis/ImageAnalysis/ImageCollapserData.h>

#include <iostream>
#include <QDebug>


ProfileCASA::ProfileCASA(QObject *parent) :
    QObject(parent),
    PIXEL_UNIT( "pix"){
}


casa::MFrequency::Types ProfileCASA::_determineRefFrame(
        std::shared_ptr<casa::ImageInterface<casa::Float> > img ) const {
    casa::MFrequency::Types freqtype = casa::MFrequency::DEFAULT;
    if ( img ){
        casa::CoordinateSystem cSys=img->coordinates();
        casa::Int specAx=cSys.findCoordinate(casa::Coordinate::SPECTRAL);
        if ( specAx >= 0 ) {
            casa::SpectralCoordinate specCoor=cSys.spectralCoordinate(specAx);
            casa::MFrequency::Types tfreqtype;
            casa::MEpoch tepoch;
            casa::MPosition tposition;
            casa::MDirection tdirection;
            specCoor.getReferenceConversion(tfreqtype, tepoch, tposition, tdirection);
            freqtype = specCoor.frequencySystem(casa::False); // false means: get the native type
        }
    }
    return freqtype;
}

Carta::Lib::Hooks::ProfileResult ProfileCASA::_generateProfile( casa::ImageInterface < casa::Float > * imagePtr,
        std::shared_ptr<Carta::Lib::Regions::RegionBase> regionInfo,
        Carta::Lib::ProfileInfo profileInfo ) const {
    std::vector<std::pair<double,double> > profileData;
    casa::CoordinateSystem cSys = imagePtr->coordinates();
    casa::uInt spectralAxis = 0;
    if ( cSys.hasSpectralAxis()){
        spectralAxis = cSys.spectralAxisNumber();
    }
    else {
        int tabCoord = cSys.findCoordinate( casa::Coordinate::TABULAR );
        if ( tabCoord >= 0 ){
            spectralAxis = tabCoord;
        }
    }
    Carta::Lib::Hooks::ProfileResult profileResult;

    //Get the requested rest frequency & unit
    double restFrequency = profileInfo.getRestFrequency();
    QString restUnit = profileInfo.getRestUnit();

    //No rest frequency was specified so use the rest frequency from the image.
    if ( restUnit.trimmed().length() == 0 ){

        //Fill in the image rest frequency & unit
        if ( cSys.hasSpectralAxis() ){
            double restFrequencyImage = cSys.spectralCoordinate().restFrequency();
            QString restUnitImage = cSys.spectralCoordinate().worldAxisUnits()[0].c_str();
            profileResult.setRestUnits( restUnitImage );
            profileResult.setRestFrequency( restFrequencyImage );
            restFrequency = restFrequencyImage;
            restUnit = restUnitImage;
        }
    }

    QString shape = regionInfo->typeName();
    QPolygonF regionCorners = regionInfo->outlineBox();

    int cornerCount = regionCorners.size();
    casa::Vector<casa::Double> x(cornerCount);
    casa::Vector<casa::Double> y(cornerCount);
    for ( int i = 0; i < cornerCount; i++ ){
        QPointF corner = regionCorners.value( i );
        x[i] = corner.x();
        y[i] = corner.y();
    }
    casa::Record regionRecord = _getRegionRecord( shape, cSys, x, y);
    QString spectralType = profileInfo.getSpectralType();
    QString spectralUnit = profileInfo.getSpectralUnit();
    if ( spectralType == "Channel"){
        spectralUnit = PIXEL_UNIT;
        spectralType = "default";
    }
    casa::String pixelSpectralType( spectralType.toStdString().c_str() );

    casa::String unit( spectralUnit.toStdString().c_str() );
    casa::PixelValueManipulatorData::SpectralType specType
        = casa::PixelValueManipulatorData::spectralType( pixelSpectralType );
    casa::Vector<casa::Float> jyValues;
    casa::Vector<casa::Double> xValues;
    try {
        std::shared_ptr<casa::ImageInterface<casa::Float> >image ( imagePtr->cloneII() );
        casa::PixelValueManipulator<casa::Float> pvm(image, &regionRecord, "");
        casa::ImageCollapserData::AggregateType funct = _getCombineMethod( profileInfo );
        casa::MFrequency::Types freqType = _determineRefFrame( image );
        casa::String frame = casa::String( casa::MFrequency::showType( freqType));

        casa::Quantity restFreq( restFrequency, casa::Unit( restUnit.toStdString().c_str()));
        casa::Record result = pvm.getProfile( spectralAxis, funct, unit, specType,
                &restFreq, frame );

        const casa::String VALUE_KEY( "values");
        if ( result.isDefined( VALUE_KEY )){
            result.get( VALUE_KEY, jyValues );
        }

        const casa::String X_KEY( "coords");
        if ( result.isDefined( X_KEY )){
            result.get( X_KEY, xValues );
        }

        int dataCount = jyValues.size();
        for ( int i = 0; i < dataCount; i++ ){
            std::pair<double,double> dataPair(xValues[i], jyValues[i]);
            profileData.push_back( dataPair );
        }
        profileResult.setData( profileData );
    }
    catch( casa::AipsError& error ){
        qDebug() << "Could not generate profile: "<<error.getMesg().c_str();
    }
    return profileResult;
}


casa::ImageCollapserData::AggregateType ProfileCASA::_getCombineMethod( Carta::Lib::ProfileInfo profileInfo ) const {
    Carta::Lib::ProfileInfo::AggregateType combineType = profileInfo.getAggregateType();
    casa::ImageCollapserData::AggregateType collapseType = casa::ImageCollapserData::AggregateType::MEAN;
    if ( combineType == Carta::Lib::ProfileInfo::AggregateType::MEDIAN ){
        collapseType = casa::ImageCollapserData::AggregateType::MEDIAN;
    }
    else if ( combineType == Carta::Lib::ProfileInfo::AggregateType::SUM ){
        collapseType = casa::ImageCollapserData::AggregateType::SUM;
    }
    else if ( combineType == Carta::Lib::ProfileInfo::AggregateType::VARIANCE ){
        collapseType = casa::ImageCollapserData::AggregateType::VARIANCE;
    }
    else if ( combineType == Carta::Lib::ProfileInfo::AggregateType::MIN ){
        collapseType = casa::ImageCollapserData::AggregateType::MIN;
    }
    else if ( combineType == Carta::Lib::ProfileInfo::AggregateType::MAX ){
        collapseType = casa::ImageCollapserData::AggregateType::MAX;
    }
    else if ( combineType == Carta::Lib::ProfileInfo::AggregateType::RMS ){
        collapseType = casa::ImageCollapserData::AggregateType::RMS;
    }
    else if ( combineType == Carta::Lib::ProfileInfo::AggregateType::FLUX_DENSITY ){
        collapseType = casa::ImageCollapserData::AggregateType::FLUX;
    }
    return collapseType;
}


casa::ImageRegion* ProfileCASA::_getEllipsoid(const casa::CoordinateSystem& cSys,
        const casa::Vector<casa::Double>& x, const casa::Vector<casa::Double>& y) const {
    casa::Vector<casa::Quantity> center(2);
    casa::Vector<casa::Quantity> radius(2);
    casa::ImageRegion* imageRegion = NULL;
    if ( x.size() == 2 && y.size() == 2 ){
        const casa::String pixUnits( PIXEL_UNIT.toStdString().c_str() );
        center[0] = casa::Quantity( (x[0]+x[1])/2, pixUnits );
        center[1] = casa::Quantity( (y[0]+y[1])/2, pixUnits );
        casa::MDirection::Types type = casa::MDirection::N_Types;
        int directionIndex = cSys.findCoordinate( casa::Coordinate::DIRECTION );
        if ( directionIndex >= 0 ){
            casa::uInt dirIndex = static_cast<casa::uInt>(directionIndex);
            type = cSys.directionCoordinate(dirIndex).directionType(true);

            casa::Vector<casa::Double> qCenter(2);
            qCenter[0] = center[0].getValue();
            qCenter[1] = center[1].getValue();
            const casa::String angleUnits( "deg");
            casa::MDirection mdcenter( casa::Quantum<casa::Vector<casa::Double> >(qCenter,angleUnits), type );

            casa::Vector<casa::Double> blc_pix_x(2);
            blc_pix_x[0] = x[0];
            blc_pix_x[1] = center[1].getValue();
            casa::MDirection mdblc_x( casa::Quantum<casa::Vector<casa::Double> >(blc_pix_x,angleUnits),type );

            casa::Vector<casa::Double> blc_pix_y(2);
            blc_pix_y[0] = center[0].getValue();
            blc_pix_y[1] = y[0];
            casa::MDirection mdblc_y( casa::Quantum<casa::Vector<casa::Double> >(blc_pix_y,angleUnits),type );

            double xdistance = mdcenter.getValue( ).separation(mdblc_x.getValue( ));
            double ydistance = mdcenter.getValue( ).separation(mdblc_y.getValue( ));
            const float ERR = 0;
            if ( xdistance > ERR && ydistance > ERR ){
                radius[0] = casa::Quantity(xdistance, pixUnits );
                radius[1] = casa::Quantity(ydistance, pixUnits );

                casa::Vector<casa::Int> pixax(2);
                casa::Vector<casa::Int> dirPixelAxis = cSys.pixelAxes(directionIndex);
                pixax(0) = dirPixelAxis[0];
                pixax(1) = dirPixelAxis[1];
                casa::WCEllipsoid ellipsoid( center, radius, casa::IPosition(dirPixelAxis), cSys);
                imageRegion = new casa::ImageRegion( ellipsoid );
            }
        }
    }
    else {
        qDebug() << "Invalid size (2) for an ellipse: "<<x.size()<<" and "<<y.size();
    }
    return imageRegion;
}


std::vector<HookId> ProfileCASA::getInitialHookList(){
    return {
        Carta::Lib::Hooks::Initialize::staticId,
        Carta::Lib::Hooks::ProfileHook::staticId
    };
}


casa::ImageRegion* ProfileCASA::_getPolygon(const casa::CoordinateSystem& cSys,
        const casa::Vector<casa::Double>& x, const casa::Vector<casa::Double>& y) const {
    casa::ImageRegion* polygon = NULL;
    const casa::String pixUnits( PIXEL_UNIT.toStdString().c_str());
    casa::RegionManager regMan;
    int n = x.size();
    casa::Vector<casa::Quantity> xvertex(n);
    casa::Vector<casa::Quantity> yvertex(n);
    for (casa::Int k = 0; k < n; ++k) {
        xvertex[k] = casa::Quantity(x[k], pixUnits);
        yvertex[k] = casa::Quantity(y[k], pixUnits);
    }
    int directionIndex = cSys.findCoordinate( casa::Coordinate::DIRECTION );
    if ( directionIndex >= 0 ){
        casa::Vector<casa::Int> dirPixelAxis = cSys.pixelAxes(directionIndex);
        casa::Vector<casa::Int> pixax(2);
        pixax(0) = dirPixelAxis[0];
        pixax(1) = dirPixelAxis[1];
        polygon = regMan.wpolygon(xvertex, yvertex, pixax, cSys, "abs");
    }
    return polygon;
}


casa::Record ProfileCASA::_getRegionRecord( const QString& shape, const casa::CoordinateSystem& cSys,
        const casa::Vector<casa::Double>& x, const casa::Vector<casa::Double>& y) const {
    const casa::String pixUnits( PIXEL_UNIT.toStdString().c_str());
    const casa::String absStr( "abs");
    casa::Record regionRecord;
    casa::Int directionIndex = cSys.findCoordinate(casa::Coordinate::DIRECTION);
    if ( directionIndex >= 0 ){
        casa::Vector<casa::Int> dirPixelAxis = cSys.pixelAxes(directionIndex);
        casa::RegionManager regMan;
        if ( shape == Carta::Lib::Regions::Polygon::TypeName ){
            int ptCount = x.size();
            if ( ptCount == 4 ){
                casa::Vector<casa::Quantity> blc(2);
                casa::Vector<casa::Quantity> trc(2);
                blc(0) = casa::Quantity(x[0], pixUnits);
                blc(1) = casa::Quantity(y[0], pixUnits);
                trc(0) = casa::Quantity(x[1], pixUnits);
                trc(1) = casa::Quantity(y[1], pixUnits);
                casa::Vector<casa::Int> pixax(2);
                pixax(0) = dirPixelAxis[0];
                pixax(1) = dirPixelAxis[1];

                casa::Record* imagregRecord = regMan.wbox(blc, trc, pixax, cSys, absStr, pixUnits);
                regionRecord = *imagregRecord;
                delete imagregRecord;
            }
            else if ( ptCount == 1 ){
                //Try a rectangle with blc=trc;
                casa::Vector<casa::Quantity> blc(2);
                casa::Vector<casa::Quantity> trc(2);
                blc(0) = casa::Quantity(x[0], pixUnits);
                blc(1) = casa::Quantity(y[0], pixUnits);
                trc(0) = casa::Quantity(x[0], pixUnits);
                trc(1) = casa::Quantity(y[0], pixUnits);
                casa::Vector<casa::Int> pixax(2);
                pixax(0) = dirPixelAxis[0];
                pixax(1) = dirPixelAxis[1];

                casa::Record* imagregRecord = regMan.wbox(blc, trc, pixax, cSys, absStr, pixUnits);
                regionRecord=*imagregRecord;
                delete imagregRecord;
            }
            else if ( ptCount > 0 ){
                casa::ImageRegion* polygon = _getPolygon( cSys, x, y );
                if ( polygon != NULL ){
                    regionRecord = polygon->toRecord(casa::String(""));
                    delete polygon;
                }
            }
            else {
                qDebug() << "Profile Error: A region must have at least one point.";
            }
        }
        else if ( shape == Carta::Lib::Regions::Circle::TypeName ){
            casa::ImageRegion* ellipsoid = _getEllipsoid( cSys, x, y );
            if ( ellipsoid != NULL ){
                regionRecord = ellipsoid->toRecord("");
                delete ellipsoid;
            }
        }
    }
    return regionRecord;
}


bool ProfileCASA::handleHook(BaseHook & hookData){
    qDebug() << "ProfileCASA plugin is handling hook #" << hookData.hookId();
    if( hookData.is<Carta::Lib::Hooks::Initialize>()) {
        return true;
    }
    else if ( hookData.is<Carta::Lib::Hooks::ProfileHook>()){
        Carta::Lib::Hooks::ProfileHook & hook
            = static_cast<Carta::Lib::Hooks::ProfileHook &>( hookData);

        std::shared_ptr<Carta::Lib::Image::ImageInterface> imagePtr = hook.paramsPtr->m_dataSource;

        if ( !imagePtr ) {
            return false;
        }

        casa::ImageInterface < casa::Float > * casaImage = cartaII2casaII_float( imagePtr );
        if( ! casaImage) {
            qWarning() << "Profile plugin: not an image created by casaimageloader...";
            return false;
        }

        std::shared_ptr<Carta::Lib::Regions::RegionBase> regionInfo = hook.paramsPtr->m_regionInfo;
        Carta::Lib::ProfileInfo profileInfo = hook.paramsPtr->m_profileInfo;
        hook.result = _generateProfile( casaImage, regionInfo, profileInfo );
        return true;
    }
    qWarning() << "Sorry, ProfileCASA doesn't know how to handle this hook";
    return false;
}


ProfileCASA::~ProfileCASA(){

}
