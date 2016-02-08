#include "Profiler.h"
#include "CurveData.h"
#include "IntensityUnits.h"
#include "SpectralUnits.h"
#include "Data/Clips.h"
#include "Data/Settings.h"
#include "Data/LinkableImpl.h"
#include "Data/Image/Controller.h"
#include "Data/Image/DataSource.h"
#include "Data/Error/ErrorManager.h"
#include "Data/Util.h"
#include "Data/Plotter/LegendLocations.h"
#include "Data/Plotter/Plot2DManager.h"
#include "Data/Plotter/LineStyles.h"
#include "Data/Plotter/PlotStyles.h"
#include "Plot2D/Plot2DGenerator.h"

#include "CartaLib/Hooks/Plot2DResult.h"
#include "CartaLib/Hooks/ConversionIntensityHook.h"
#include "CartaLib/Hooks/ConversionSpectralHook.h"
#include "CartaLib/AxisInfo.h"
#include "State/UtilState.h"
#include "Globals.h"
#include "PluginManager.h"
#include <QDebug>

namespace Carta {

namespace Data {

const QString Profiler::CLASS_NAME = "Profiler";
const QString Profiler::AXIS_UNITS_BOTTOM = "axisUnitsBottom";
const QString Profiler::AXIS_UNITS_LEFT = "axisUnitsLeft";
const QString Profiler::CURVES = "curves";
const QString Profiler::LEGEND_LOCATION = "legendLocation";
const QString Profiler::LEGEND_EXTERNAL = "legendExternal";
const QString Profiler::LEGEND_SHOW = "legendShow";
const QString Profiler::LEGEND_LINE = "legendLine";
const QString Profiler::TAB_INDEX = "tabIndex";


class Profiler::Factory : public Carta::State::CartaObjectFactory {
public:
    Carta::State::CartaObject * create (const QString & path, const QString & id){
        return new Profiler (path, id);
    }
};

bool Profiler::m_registered =
        Carta::State::ObjectManager::objectManager()->registerClass ( CLASS_NAME, new Profiler::Factory());

SpectralUnits* Profiler::m_spectralUnits = nullptr;
IntensityUnits* Profiler::m_intensityUnits = nullptr;


QList<QColor> Profiler::m_curveColors = {Qt::blue, Qt::green, Qt::black, Qt::cyan,
        Qt::magenta, Qt::yellow, Qt::gray };



using Carta::State::UtilState;
using Carta::State::StateInterface;
using Carta::Plot2D::Plot2DGenerator;

Profiler::Profiler( const QString& path, const QString& id):
            CartaObject( CLASS_NAME, path, id ),
            m_linkImpl( new LinkableImpl( path )),
            m_preferences( nullptr),
            m_plotManager( new Plot2DManager( path, id ) ),
            m_legendLocations( nullptr),
            m_stateData( UtilState::getLookup(path, StateInterface::STATE_DATA)){

    m_oldFrame = 0;
    m_currentFrame = 0;
    m_timerId = 0;

    Carta::State::ObjectManager* objMan = Carta::State::ObjectManager::objectManager();
    Settings* prefObj = objMan->createObject<Settings>();
    m_preferences.reset( prefObj );

    LegendLocations* legObj = objMan->createObject<LegendLocations>();
    m_legendLocations.reset( legObj );

    m_plotManager->setPlotGenerator( new Plot2DGenerator( Plot2DGenerator::PlotType::PROFILE) );
    m_plotManager->setTitleAxisY( "" );
    connect( m_plotManager.get(), SIGNAL(userSelectionColor()), this, SLOT(_movieFrame()));

    _initializeStatics();
    _initializeDefaultState();
    _initializeCallbacks();

    m_controllerLinked = false;
}


QString Profiler::addLink( CartaObject*  target){
    Controller* controller = dynamic_cast<Controller*>(target);
    bool linkAdded = false;
    QString result;
    if ( controller != nullptr ){
        if ( !m_controllerLinked ){
            linkAdded = m_linkImpl->addLink( controller );
            if ( linkAdded ){
                connect(controller, SIGNAL(dataChanged(Controller*)), this , SLOT(_generateProfile(Controller*)));
                connect(controller, SIGNAL(frameChanged(Controller*, Carta::Lib::AxisInfo::KnownType)),
                        this, SLOT( _updateChannel(Controller*, Carta::Lib::AxisInfo::KnownType)));
                m_controllerLinked = true;
                _generateProfile( controller );
            }
        }
        else {
            CartaObject* obj = m_linkImpl->searchLinks( target->getPath());
            if ( obj != nullptr ){
                linkAdded = true;
            }
            else {
                result = "Profiler only supports linking to a single image source.";
            }
        }
    }
    else {
        result = "Profiler only supports linking to images";
    }
    return result;
}


void Profiler::_assignColor( std::shared_ptr<CurveData> curveData ){
    //First go through list of fixed colors & see if there is one available.
    int fixedColorCount = m_curveColors.size();
    int curveCount = m_plotCurves.size();
    bool colorAssigned = false;
    for ( int i = 0; i < fixedColorCount; i++ ){
        bool colorAvailable = true;
        QString fixedColorName = m_curveColors[i].name();
        for ( int j = 0; j < curveCount; j++ ){
            if ( m_plotCurves[j]->getColor().name() == fixedColorName ){
                colorAvailable = false;
                break;
            }
        }
        if ( colorAvailable ){
            curveData->setColor( m_curveColors[i] );
            colorAssigned = true;
            break;
        }
    }

    //If there is no color in the fixed list, assign a random one.
    if ( !colorAssigned ){
        const int MAX_COLOR = 255;
        int redAmount = qrand() % MAX_COLOR;
        int greenAmount = qrand() % MAX_COLOR;
        int blueAmount = qrand() % MAX_COLOR;
        QColor randomColor( redAmount, greenAmount, blueAmount );
        curveData->setColor( randomColor.name());
    }
}


std::vector<double> Profiler::_convertUnitsX( std::shared_ptr<CurveData> curveData,
        const QString& newUnit ) const {
    QString bottomUnit = newUnit;
    if ( newUnit.isEmpty() ){
        bottomUnit = m_state.getValue<QString>( AXIS_UNITS_BOTTOM );
    }
    std::vector<double> converted = curveData->getValuesX();
    std::shared_ptr<Carta::Lib::Image::ImageInterface> dataSource = curveData->getSource();
    if ( ! m_bottomUnit.isEmpty() ){
        if ( bottomUnit != m_bottomUnit ){
            QString oldUnit = _getUnitUnits( m_bottomUnit );
            QString newUnit = _getUnitUnits( bottomUnit );
            _convertX ( converted, dataSource, oldUnit, newUnit );
        }
    }
    return converted;
}

void Profiler::_convertX( std::vector<double>& converted,
        std::shared_ptr<Carta::Lib::Image::ImageInterface> dataSource,
        const QString& oldUnit, const QString& newUnit ) const {
    if ( dataSource ){
        auto result = Globals::instance()-> pluginManager()
                             -> prepare <Carta::Lib::Hooks::ConversionSpectralHook>(dataSource,
                                     oldUnit, newUnit, converted );
        auto lam = [&converted] ( const Carta::Lib::Hooks::ConversionSpectralHook::ResultType &data ) {
            converted = data;
        };
        try {
            result.forEach( lam );
        }
        catch( char*& error ){
            QString errorStr( error );
            ErrorManager* hr = Util::findSingletonObject<ErrorManager>();
            hr->registerError( errorStr );
        }
    }
}


std::vector<double> Profiler::_convertUnitsY( std::shared_ptr<CurveData> curveData ) const {
    std::vector<double> converted = curveData->getValuesY();
    std::vector<double> plotDataX = curveData->getValuesX();
    QString leftUnit = m_state.getValue<QString>( AXIS_UNITS_LEFT );
    if ( ! m_leftUnit.isEmpty() ){
        Controller* controller = _getControllerSelected();
        if ( controller ){
            if ( leftUnit != m_leftUnit ){
                std::shared_ptr<Carta::Lib::Image::ImageInterface> dataSource =
                        curveData->getSource();
                if ( dataSource > 0 ){
                    //First, we need to make sure the x-values are in Hertz.
                    std::vector<double> hertzVals = _convertUnitsX( curveData, "Hz");
                    bool validBounds = false;
                    std::pair<double,double> boundsY = m_plotManager->getPlotBoundsY( curveData->getName(), &validBounds );
                    if ( validBounds ){
                        QString maxUnit = m_plotManager->getAxisUnitsY();
                        auto result = Globals::instance()-> pluginManager()
                             -> prepare <Carta::Lib::Hooks::ConversionIntensityHook>(dataSource,
                                                         m_leftUnit, leftUnit, hertzVals, converted,
                                                         boundsY.second, maxUnit );;

                        auto lam = [&converted] ( const Carta::Lib::Hooks::ConversionIntensityHook::ResultType &data ) {
                            converted = data;
                        };
                        try {
                            result.forEach( lam );
                        }
                        catch( char*& error ){
                            QString errorStr( error );
                            ErrorManager* hr = Util::findSingletonObject<ErrorManager>();
                            hr->registerError( errorStr );
                        }
                    }
                }
            }
        }
    }
    return converted;
}


int Profiler::_findCurveIndex( const QString& curveName ) const {
    int curveCount = m_plotCurves.size();
    int index = -1;
    for ( int i = 0; i < curveCount; i++ ){
        if ( m_plotCurves[i]->getName() == curveName ){
            index = i;
            break;
        }
    }
    return index;
}


void Profiler::_generateProfile(Controller* controller ){
    Controller* activeController = controller;
    if ( activeController == nullptr ){
        activeController = _getControllerSelected();
    }
    if ( activeController ){
        _loadProfile( activeController );
    }
}


Controller* Profiler::_getControllerSelected() const {
    //We are only supporting one linked controller.
    Controller* controller = nullptr;
    int linkCount = m_linkImpl->getLinkCount();
    for ( int i = 0; i < linkCount; i++ ){
        CartaObject* obj = m_linkImpl->getLink(i );
        Controller* control = dynamic_cast<Controller*>( obj);
        if ( control != nullptr){
            controller = control;
            break;
        }
    }
    return controller;
}


QString Profiler::getStateString( const QString& sessionId, SnapshotType type ) const{
    QString result("");
    if ( type == SNAPSHOT_PREFERENCES ){
        StateInterface prefState( "");
        prefState.setValue<QString>(Carta::State::StateInterface::OBJECT_TYPE, CLASS_NAME );
        prefState.insertValue<QString>(Util::PREFERENCES, m_state.toString());
        prefState.insertValue<QString>( Settings::SETTINGS, m_preferences->getStateString(sessionId, type) );
        result = prefState.toString();
    }
    else if ( type == SNAPSHOT_LAYOUT ){
        result = m_linkImpl->getStateString(getIndex(), getSnapType( type ));
    }
    return result;
}


QString Profiler::_getLegendLocationsId() const {
    return m_legendLocations->getPath();
}


QList<QString> Profiler::getLinks() const {
    return m_linkImpl->getLinkIds();
}


QString Profiler::_getPreferencesId() const {
    return m_preferences->getPath();
}

QString Profiler::_getUnitType( const QString& unitStr ){
    QString unitType = unitStr;
    int unitStart = unitStr.indexOf( "(");
    if ( unitStart >= 0 ){
        unitType = unitStr.mid( 0, unitStart );
    }
    return unitType;
}


QString Profiler::_getUnitUnits( const QString& unitStr ){
    QString strippedUnit = "";
    int unitStart = unitStr.indexOf( "(");
    if ( unitStart >= 0 ){
        int substrLength = unitStr.length() - unitStart - 2;
        if ( substrLength > 0){
            strippedUnit = unitStr.mid( unitStart + 1, substrLength );
        }
    }
    return strippedUnit;
}


void Profiler::_initializeDefaultState(){
    //Data state is the curves
    m_stateData.insertArray( CURVES, 0 );
    m_stateData.flushState();

    //Default units
    m_bottomUnit = m_spectralUnits->getDefault();
    QString unitType = _getUnitType( m_bottomUnit );
    m_plotManager->setTitleAxisX( unitType );
    m_state.insertValue<QString>( AXIS_UNITS_BOTTOM, m_bottomUnit );
    m_state.insertValue<QString>( AXIS_UNITS_LEFT, m_intensityUnits->getDefault());

    //Legend
    bool external = true;
    QString legendLoc = m_legendLocations->getDefaultLocation( external );
    m_state.insertValue<QString>( LEGEND_LOCATION, legendLoc );
    m_state.insertValue<bool>( LEGEND_EXTERNAL, external );
    m_state.insertValue<bool>( LEGEND_SHOW, true );
    m_state.insertValue<bool>( LEGEND_LINE, true );

    //Default Tab
    m_state.insertValue<int>( TAB_INDEX, 2 );

    m_state.flushState();
}


void Profiler::_initializeCallbacks(){

    addCommandCallback( "registerLegendLocations", [=] (const QString & /*cmd*/,
            const QString & /*params*/, const QString & /*sessionId*/) -> QString {
        QString result = _getLegendLocationsId();
        return result;
    });

    addCommandCallback( "registerPreferences", [=] (const QString & /*cmd*/,
            const QString & /*params*/, const QString & /*sessionId*/) -> QString {
        QString result = _getPreferencesId();
        return result;
    });


    addCommandCallback( "setAxisUnitsBottom", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {Util::UNITS};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString unitStr = dataValues[*keys.begin()];
        QString result = setAxisUnitsBottom( unitStr );
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setAxisUnitsLeft", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {Util::UNITS};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString unitStr = dataValues[*keys.begin()];
        QString result = setAxisUnitsLeft( unitStr );
        Util::commandPostProcess( result );
        return result;
    });



    addCommandCallback( "setCurveColor", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        QString result;
        std::set<QString> keys = {Util::RED, Util::GREEN, Util::BLUE, Util::NAME};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString redStr = dataValues[Util::RED];
        QString greenStr = dataValues[Util::GREEN];
        QString blueStr = dataValues[Util::BLUE];
        QString curveName = dataValues[Util::NAME];
        bool validRed = false;
        int redAmount = redStr.toInt( &validRed );
        bool validGreen = false;
        int greenAmount = greenStr.toInt( &validGreen );
        bool validBlue = false;
        int blueAmount = blueStr.toInt( &validBlue );
        if ( validRed && validGreen && validBlue ){
            QStringList resultList = setCurveColor( curveName, redAmount, greenAmount, blueAmount );
            result = resultList.join( ";");
        }
        else {
            result = "Please check that curve colors are integers: " + params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setLegendLocation", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {LEGEND_LOCATION};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString locationStr = dataValues[LEGEND_LOCATION];
        QString result = setLegendLocation( locationStr );
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setLegendExternal", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {LEGEND_EXTERNAL};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString externalStr = dataValues[LEGEND_EXTERNAL];
        bool validBool = false;
        bool externalLegend = Util::toBool( externalStr, &validBool );
        QString result;
        if ( validBool ){
            setLegendExternal( externalLegend );
        }
        else {
            result = "Setting the legend external to the plot must be true/false: "+params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setLegendShow", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {LEGEND_SHOW};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString showStr = dataValues[LEGEND_SHOW];
        bool validBool = false;
        bool show = Util::toBool( showStr, &validBool );
        QString result;
        if ( validBool ){
            setLegendShow( show );
        }
        else {
            result = "Set show legend must be true/false: "+params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setLegendLine", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {LEGEND_LINE};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString showStr = dataValues[LEGEND_LINE];
        bool validBool = false;
        bool show = Util::toBool( showStr, &validBool );
        QString result;
        if ( validBool ){
            setLegendLine( show );
        }
        else {
            result = "Set show legend line must be true/false: "+params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setLineStyle", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {CurveData::STYLE, Util::NAME};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString lineStyle = dataValues[CurveData::STYLE];
        QString curveName = dataValues[Util::NAME];
        QString result = setLineStyle( curveName, lineStyle );
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setTabIndex", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        QString result;
        std::set<QString> keys = {TAB_INDEX};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString tabIndexStr = dataValues[TAB_INDEX];
        bool validIndex = false;
        int tabIndex = tabIndexStr.toInt( &validIndex );
        if ( validIndex ){
            result = setTabIndex( tabIndex );
        }
        else {
            result = "Please check that the tab index is a number: " + params;
        }
        Util::commandPostProcess( result );
        return result;
    });
}


void Profiler::_initializeStatics(){
    if ( m_spectralUnits == nullptr ){
        m_spectralUnits = Util::findSingletonObject<SpectralUnits>();
    }
    if ( m_intensityUnits == nullptr ){
        m_intensityUnits = Util::findSingletonObject<IntensityUnits>();
    }
}


bool Profiler::isLinked( const QString& linkId ) const {
    bool linked = false;
    CartaObject* obj = m_linkImpl->searchLinks( linkId );
    if ( obj != nullptr ){
        linked = true;
    }
    return linked;
}


void Profiler::_loadProfile( Controller* controller ){
    if( ! controller) {
        return;
    }
    std::vector<std::shared_ptr<DataSource> > dataSources = controller->getDataSources();

    m_plotCurves.clear();
    int dataCount = dataSources.size();
    for ( int i = 0; i < dataCount; i++ ) {
        std::shared_ptr<Carta::Lib::Image::ImageInterface> image = dataSources[i]->_getImage();
    	std::vector < int > pos( image-> dims().size(), 0 );
        int axis = Util::getAxisIndex( image, Carta::Lib::AxisInfo::KnownType::SPECTRAL );
        Profiles::PrincipalAxisProfilePath path( axis, pos );
        Carta::Lib::NdArray::RawViewInterface * rawView = image-> getDataSlice( SliceND() );
        Profiles::ProfileExtractor * extractor = new Profiles::ProfileExtractor( rawView );
        shared_ptr<Carta::Lib::Image::MetaDataInterface> metaData = image->metaData();
        QString fileName = metaData->title();
        m_leftUnit = image->getPixelUnit().toStr();

        auto profilecb = [ = ] () {
            bool finished = extractor->isFinished();
            if ( finished ){
                auto data = extractor->getDataD();

                int dataCount = data.size();
                if ( dataCount > 0 ){
                    std::vector<double> plotDataX( dataCount );
                    std::vector<double> plotDataY( dataCount );

                    for( int i = 0 ; i < dataCount; i ++ ){
                        plotDataX[i] = i;
                        plotDataY[i] = data[i];
                    }

                    int curveIndex = _findCurveIndex( fileName );
                    std::shared_ptr<CurveData> profileCurve( nullptr );
                    if ( curveIndex < 0 ){
                        Carta::State::ObjectManager* objMan = Carta::State::ObjectManager::objectManager();
                        profileCurve.reset( objMan->createObject<CurveData>() );
                        profileCurve->setName( fileName );
                        _assignColor( profileCurve );
                        m_plotCurves.append( profileCurve );
                        profileCurve->setSource( image );
                        _saveCurveState();
                    }
                    else {
                        profileCurve = m_plotCurves[curveIndex];
                    }
                    profileCurve->setData( plotDataX, plotDataY );
                    _updatePlotData();
                }
                extractor->deleteLater();
            }
        };
        connect( extractor, & Profiles::ProfileExtractor::progress, profilecb );
        extractor-> start( path );
    }
}



void Profiler::_movieFrame(){
    //Get the new frame from the plot
    bool valid = false;
    double xLocation = qRound( m_plotManager -> getVLinePosition(&valid));
    if ( valid ){
        //Need to convert the xLocation to a frame number.
        if ( m_plotCurves.size() > 0 ){
            std::vector<double> val(1);
            val[0] = xLocation;
            QString oldUnits = m_state.getValue<QString>( AXIS_UNITS_BOTTOM );
            QString basicUnit = _getUnitUnits( oldUnits );
            if ( !basicUnit.isEmpty() ){
                _convertX( val, m_plotCurves[0]->getSource(), basicUnit, "");
            }
            Controller* controller = _getControllerSelected();
            if ( controller && m_timerId == 0 ){
                int oldFrame = controller->getFrame( Carta::Lib::AxisInfo::KnownType::SPECTRAL );
                if ( oldFrame != val[0] ){
                    m_oldFrame = oldFrame;
                    m_currentFrame = val[0];
                    m_timerId = startTimer( 1000 );
                }
            }
        }
    }
}


QString Profiler::removeLink( CartaObject* cartaObject){
    bool removed = false;
    QString result;
    Controller* controller = dynamic_cast<Controller*>( cartaObject );
    if ( controller != nullptr ){
        removed = m_linkImpl->removeLink( controller );
        if ( removed ){
            controller->disconnect(this);
            m_controllerLinked = false;
            //_resetDefaultStateData();
        }
    }
    else {
       result = "Profiler was unable to remove link only image links are supported";
    }
    return result;
}

void Profiler::resetState( const QString& state ){
    StateInterface restoredState( "");
    restoredState.setState( state );

    QString settingStr = restoredState.getValue<QString>(Settings::SETTINGS);
    m_preferences->resetStateString( settingStr );

    QString prefStr = restoredState.getValue<QString>(Util::PREFERENCES);
    m_state.setState( prefStr );
    m_state.flushState();
}

void Profiler::_saveCurveState( int index ){
    QString key = Carta::State::UtilState::getLookup( CURVES, index );
    QString curveState = m_plotCurves[index]->getStateString();
    m_stateData.setObject( key, curveState );
}

void Profiler::_saveCurveState(){
    int curveCount = m_plotCurves.size();
    m_stateData.resizeArray( CURVES, curveCount );
    for ( int i = 0; i < curveCount; i++ ){
       _saveCurveState( i );
    }
    m_stateData.flushState();
}

QString Profiler::setAxisUnitsBottom( const QString& unitStr ){
    QString result;
    QString actualUnits = m_spectralUnits->getActualUnits( unitStr );
    if ( !actualUnits.isEmpty() ){
        QString oldBottomUnits = m_state.getValue<QString>( AXIS_UNITS_BOTTOM );
        if ( actualUnits != oldBottomUnits ){
            m_state.setValue<QString>( AXIS_UNITS_BOTTOM, actualUnits);
            m_plotManager->setTitleAxisX( _getUnitType( actualUnits ) );
            m_state.flushState();
            _updatePlotData();
            _updateChannel( _getControllerSelected(), Carta::Lib::AxisInfo::KnownType::SPECTRAL );
        }
    }
    else {
        result = "Unrecognized profile bottom axis units: "+unitStr;
    }
    return result;
}

QString Profiler::setAxisUnitsLeft( const QString& unitStr ){
    QString result;
    QString actualUnits = m_intensityUnits->getActualUnits( unitStr );
    if ( !actualUnits.isEmpty() ){
        QString oldLeftUnits = m_state.getValue<QString>( AXIS_UNITS_LEFT );
        if ( oldLeftUnits != actualUnits ){
            m_state.setValue<QString>( AXIS_UNITS_LEFT, actualUnits );
            m_state.flushState();
            _updatePlotData();
            _updateChannel( _getControllerSelected(), Carta::Lib::AxisInfo::KnownType::SPECTRAL );
            m_plotManager->setTitleAxisY( actualUnits );
        }
    }
    else {
        result = "Unrecognized profile left axis units: "+unitStr;
    }
    return result;
}


QStringList Profiler::setCurveColor( const QString& name, int redAmount, int greenAmount, int blueAmount ){
    QStringList result;
    const int MAX_COLOR = 255;
    bool validColor = true;
    if ( redAmount < 0 || redAmount > MAX_COLOR ){
        validColor = false;
        result.append("Profile curve red amount must be in [0,"+QString::number(MAX_COLOR)+"]: "+QString::number(redAmount) );
    }
    if ( greenAmount < 0 || greenAmount > MAX_COLOR ){
        validColor = false;
        result.append("Profile curve green amount must be in [0,"+QString::number(MAX_COLOR)+"]: "+QString::number(greenAmount) );
    }
    if ( blueAmount < 0 || blueAmount > MAX_COLOR ){
        validColor = false;
        result.append("Profile curve blue amount must be in [0,"+QString::number(MAX_COLOR)+"]: "+QString::number(blueAmount) );
    }
    if ( validColor ){
        int index = _findCurveIndex( name );
        if ( index >= 0 ){
            QColor oldColor = m_plotCurves[index]->getColor();
            QColor curveColor( redAmount, greenAmount, blueAmount );
            if ( oldColor.name() != curveColor.name() ){
                m_plotCurves[index]->setColor( curveColor );
                _saveCurveState( index );
                m_stateData.flushState();
                m_plotManager->setColor( curveColor, name );
            }
        }
        else {
            result.append( "Unrecognized profile curve:"+name );
        }
    }
    return result;
}


QString Profiler::setLineStyle( const QString& name, const QString& lineStyle ){
    QString result;
    int index = _findCurveIndex( name );
    if ( index >= 0 ){
        result = m_plotCurves[index]->setLineStyle( lineStyle );
        if ( result.isEmpty() ){
            _saveCurveState( index );
            m_stateData.flushState();
            LineStyles* lineStyles = Util::findSingletonObject<LineStyles>();
            QString actualStyle = lineStyles->getActualLineStyle( lineStyle );
            m_plotManager->setLineStyle( actualStyle, name );
        }
    }
    else {
        result = "Profile curve was not recognized: "+name;
    }
    return result;
}

QString Profiler::setLegendLocation( const QString& locateStr ){
    QString result;
    QString actualLocation = m_legendLocations->getActualLocation( locateStr );
    if ( !actualLocation.isEmpty() ){
        QString oldLocation = m_state.getValue<QString>( LEGEND_LOCATION );
        if ( oldLocation != actualLocation ){
            m_state.setValue<QString>( LEGEND_LOCATION, actualLocation );
            m_state.flushState();
            m_plotManager->setLegendLocation( actualLocation );
        }
    }
    else {
        result = "Unrecognized profile legend location: "+locateStr;
    }
    return result;
}

void Profiler::setLegendExternal( bool external ){
    bool oldExternal = m_state.getValue<bool>( LEGEND_EXTERNAL );
    if ( external != oldExternal ){
        m_state.setValue<bool>( LEGEND_EXTERNAL, external );
        m_legendLocations->setAvailableLocations(external);
        //Check to see if the current location is still supported.  If not,
        //use the default.
        QString currPos = m_state.getValue<QString>( LEGEND_LOCATION );
        QString actualPos = m_legendLocations->getActualLocation( currPos );
        if ( actualPos.isEmpty() ){
            QString newPos = m_legendLocations->getDefaultLocation( external );
            m_state.setValue<QString>( LEGEND_LOCATION, newPos );
        }
        m_state.flushState();

        m_plotManager->setLegendExternal( external );
    }
}

void Profiler::setLegendLine( bool showLegendLine ){
    bool oldShowLine = m_state.getValue<bool>( LEGEND_LINE );
    if ( oldShowLine != showLegendLine ){
        m_state.setValue<bool>(LEGEND_LINE, showLegendLine );
        m_state.flushState();
        m_plotManager->setLegendLine( showLegendLine );
    }
}

void Profiler::setLegendShow( bool showLegend ){
    bool oldShowLegend = m_state.getValue<bool>( LEGEND_SHOW );
    if ( oldShowLegend != showLegend ){
        m_state.setValue<bool>(LEGEND_SHOW, showLegend );
        m_state.flushState();
        m_plotManager->setLegendShow( showLegend );
    }
}

QString Profiler::setTabIndex( int index ){
    QString result;
    if ( index >= 0 ){
        int oldIndex = m_state.getValue<int>( TAB_INDEX );
        if ( index != oldIndex ){
            m_state.setValue<int>( TAB_INDEX, index );
            m_state.flushState();
        }
    }
    else {
        result = "Profile tab index must be nonnegative: "+ QString::number(index);
    }
    return result;
}


void Profiler::timerEvent( QTimerEvent* /*event*/ ){
    Controller* controller = _getControllerSelected();
    if ( controller ){
        controller->_setFrameAxis( m_oldFrame, Carta::Lib::AxisInfo::KnownType::SPECTRAL );
        _updateChannel( controller, Carta::Lib::AxisInfo::KnownType::SPECTRAL );
        if ( m_oldFrame < m_currentFrame ){
            m_oldFrame++;
        }
        else if ( m_oldFrame > m_currentFrame ){
            m_oldFrame--;
        }
        else {
            killTimer(m_timerId );
            m_timerId = 0;
        }
    }
}


void Profiler::_updateChannel( Controller* controller, Carta::Lib::AxisInfo::KnownType type ){
    if ( type == Carta::Lib::AxisInfo::KnownType::SPECTRAL ){
        int frame = controller->getFrame( type );
        //Convert the frame to the units the plot is using.
        QString bottomUnits = m_state.getValue<QString>( AXIS_UNITS_BOTTOM );
        QString units = _getUnitUnits( bottomUnits );
        std::vector<double> values(1);
        values[0] = frame;
        if ( m_plotCurves.size() > 0 ){
            if ( !units.isEmpty() ){
                std::shared_ptr<Carta::Lib::Image::ImageInterface> imageSource = m_plotCurves[0]->getSource();
                _convertX(  values, imageSource, "", units );
            }
            m_plotManager->setVLinePosition( values[0] );
        }
    }
}


void Profiler::_updatePlotData(){
    m_plotManager->clearData();
    int curveCount = m_plotCurves.size();
    for ( int i = 0; i < curveCount; i++ ){
        //Convert the data units, if necessary.
        std::vector<double> convertedX = _convertUnitsX( m_plotCurves[i] );
        std::vector<double> convertedY = _convertUnitsY( m_plotCurves[i] );
        int dataCount = convertedX.size();
        std::vector< std::pair<double,double> > plotData(dataCount);
        for ( int i = 0; i < dataCount; i++ ){
            plotData[i].first  = convertedX[i];
            plotData[i].second = convertedY[i];
        }

        //Put the data into the plot.
        QString dataId = m_plotCurves[i]->getName();
        Carta::Lib::Hooks::Plot2DResult plotResult( dataId, "", "", plotData );
        m_plotManager->addData( &plotResult );
        m_plotManager->setColor( m_plotCurves[i]->getColor(), dataId );
    }
    QString bottomUnit = m_state.getValue<QString>( AXIS_UNITS_BOTTOM );
    bottomUnit = _getUnitUnits( bottomUnit );
    QString leftUnit = m_state.getValue<QString>( AXIS_UNITS_LEFT );
    m_plotManager->setTitleAxisX( bottomUnit );
    m_plotManager->setTitleAxisY( leftUnit );
    m_plotManager->updatePlot();
}




Profiler::~Profiler(){
}
}
}
