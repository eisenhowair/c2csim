#include "headers/SumoInterface.h"
#include <iostream>
#include <string>
#include <QObject>
#include <QDebug>
#include <QColor>
#include <QRandomGenerator>
#include <QFile>
#include <QSvgRenderer>
#include <QPixmap>
#include <QPainter>
#include <QCoreApplication>
#include <QIcon>

#include "geoconverter.h"

bool first_init = true;

SumoInterface::SumoInterface(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<QString>("QString");
    // Initialize SUMO connection here if necessary
}

SumoInterface::~SumoInterface()
{
    // Cleanup SUMO connection here if necessary
    traci.close();
}

void SumoInterface::startSimulation()
{
    // Connect to SUMO
    traci.connect("localhost", 6066);
}

void SumoInterface::stopSimulation()
{
    // Close the SUMO connection
    traci.close();
}

QColor SumoInterface::applyColor(const QString &idString)
{
    if (vehicleColors.contains(idString))
    {
        // Si oui, retourne la couleur existante
        return vehicleColors.value(idString);
    }
    else
    {
        // Sinon, génère une nouvelle couleur aléatoire, associe-la à l'ID de la voiture, puis retourne la couleur
        QColor randomColor = QColor::fromRgb(QRandomGenerator::global()->bounded(256),
                                             QRandomGenerator::global()->bounded(256),
                                             QRandomGenerator::global()->bounded(256));
        vehicleColors.insert(idString, randomColor);

        return randomColor;
    }
}

/***
 * @return ancienne vitesse de la voiture
 * @def modifie la vitesse d'une voiture ou met à l'arrêt
 */
void SumoInterface::changeSpeedCar(const QVariant &vehicleID, double speed)
{
    QString idString = vehicleID.toString();
    qDebug() << "Vehicle ID:" << idString << "New Speed:" << speed;
    traci.vehicle.setSpeed(idString.toStdString(), speed);
}

// crée des images svg de couleur différentes (une pour chaque voiture)
// et les places dans le dossier images/generated
void SumoInterface::applyColorToSVG(const QString &id)
{
    QColor carColor = applyColor(id);
    QString colorString = carColor.name(); // Obtenir le nom de la couleur (par exemple, "#RRGGBB")

    // Charger le fichier SVG
    QString originalFilePath = QCoreApplication::applicationDirPath() + "/images/car-cropped.svg";

    QFile file(originalFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qDebug() << "Erreur lors de l'ouverture du fichier SVG";
        return;
    }

    QTextStream in(&file);
    QString svgContent = in.readAll();
    file.close();

    // Modifier la couleur dans le fichier SVG
    svgContent.replace("fill=\"#000000\"", "fill=\"" + colorString + "\"");

    // Générer un nom de fichier unique en utilisant l'ID de la voiture
    QString uniqueFileName = QCoreApplication::applicationDirPath() + "/images/generated/car_modified_" + id + ".svg";

    // Sauvegarder le fichier SVG modifié avec un nom de fichier unique
    QFile modifiedFile(uniqueFileName);
    if (!modifiedFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qDebug() << "Erreur lors de la création du fichier SVG modifié";
        return;
    }

    QTextStream out(&modifiedFile);
    out << svgContent;
    modifiedFile.close();
}

double SumoInterface::recupVitesse(const QVariant &vehicleID)
{
    QString idString = vehicleID.toString();
    // qDebug() << "Current Speed:" << traci.vehicle.getSpeed(idString.toStdString());

    return traci.vehicle.getSpeed(idString.toStdString());
}

// regarde pour chaque hexagone si il y a une voiture dedans
// si il y a une voiture dedans, on récupère la couleur de la voiture,
// et on la met en argument du signal qu'on envoie au fichier main.qml

// il faudrait sûrement essayer avec un seul hexagone (par exemple le premier)
//  pour savoir s'il détecte bien les voitures, où si les coordonnées ne sont pas les bonnes
void SumoInterface::updateHexagonColor()
{
    QVariantList newHexagonColors;

    // Parcours de la liste des hexagones
    for (const QVariant &hexagonVariant : listHexagons)
    {
        QVariantMap hexagonMap = hexagonVariant.toMap();
        QString hexagonId = hexagonMap["id"].toString();
        qreal hexagonLatCenter = hexagonMap["latCenter"].toReal();
        qreal hexagonLonCenter = hexagonMap["lonCenter"].toReal();

        bool carInside = false;
        QString colorName;

        // Parcours de la liste des voitures
        for (const QVariant &voitureVariant : vehiclePositions)
        {
            QVariantMap voitureMap = voitureVariant.toMap();
            qreal voitureLat = voitureMap["latitude"].toReal();
            qreal voitureLon = voitureMap["longitude"].toReal();

            // Vérifie si la voiture est à l'intérieur de l'hexagone
            if (isPointInsideHexagon(voitureLat, voitureLon, hexagonLatCenter, hexagonLonCenter))
            {
                carInside = true;
                QVariant carColorVariant = voitureMap["color"];
                QColor color = carColorVariant.value<QColor>();
                colorName = color.name();
                break;
            }
        }

        if (carInside)
        {
            //  Met à jour la couleur de l'hexagone avec la couleur de la voiture
            newHexagonColors.append(QVariantMap{{"id", hexagonId}, {"couleur", colorName}});
        }
        else
        {
            // Met à jour la couleur de l'hexagone à transparent
            newHexagonColors.append(QVariantMap{{"id", hexagonId}, {"couleur", "transparent"}});
        }
    }

    if (newHexagonColors != hexagonColors)
    {
        hexagonColors = newHexagonColors;
        emit hexagonColorsChanged();
    }
}

// regarde si une voiture est dans un certain hexagone
bool SumoInterface::isPointInsideHexagon(qreal pointLat, qreal pointLon, qreal hexagonLatCenter, qreal hexagonLonCenter)
{
    qreal radius = 0.0025; // Adjust this value to fit your hexagons

    // Calculate the vertices of the hexagon
    QList<QPointF> hexagonVertices;
    for (int i = 0; i < 6; ++i) {
        qreal angle = 60 * i - 30;
        qreal x = hexagonLatCenter + radius * qCos(qDegreesToRadians(angle));
        qreal y = hexagonLonCenter + radius * qSin(qDegreesToRadians(angle));
        hexagonVertices.append(QPointF(x, y));
    }

    // Use the ray casting algorithm to determine if the point is inside the hexagon
    int i, j, nvert = hexagonVertices.size();
    bool inside = false;
    for (i = 0, j = nvert - 1; i < nvert; j = i++) {
        if (((hexagonVertices[i].y() > pointLon) != (hexagonVertices[j].y() > pointLon)) &&
            (pointLat < (hexagonVertices[j].x() - hexagonVertices[i].x()) * (pointLon - hexagonVertices[i].y()) / (hexagonVertices[j].y() - hexagonVertices[i].y()) + hexagonVertices[i].x())) {
            inside = !inside;
        }
    }

    return inside;
}

// on crée une liste contenant les id et coordonnées des hexagones
// pour pouvoir les utiliser dans ce fichier plus facilement
void SumoInterface::addHexagon(const QString &idHex, qreal xCenter, qreal yCenter)
{
    // GeoCoordinates result = GeoConverter::convertGeo(xCenter, yCenter);

    QVariantMap hexagonMap;
    hexagonMap["id"] = idHex;

    hexagonMap["latCenter"] = xCenter;
    hexagonMap["lonCenter"] = yCenter;

    /* tentative sans lambert
    hexagonMap["latCenter"] = result.lat;
    hexagonMap["lonCenter"] = result.lon;
    */
    listHexagons.append(hexagonMap);

    // qDebug() << "Adding hexagon with ID:" << idHex << "at (" << xCenter << "," << yCenter << ")";
    // qDebug() << "Adding hexagonLambert with ID:" << idHex << "at (" << hexagonMap["latCenter"] << "," << hexagonMap["lonCenter"] << ")";
}

QVariantList SumoInterface::getVehiclePositions() const
{
    return vehiclePositions;
}

QVariantList SumoInterface::getHexagonColors() const
{
    return hexagonColors;
}

void SumoInterface::updateVehiclePositions()
{
    QVariantList newPositions;

    // Step the simulation forward
    // qDebug() << "Stepping the simulation";
    traci.simulationStep();
    // qDebug() << "Simulation stepped";

    // Get the IDs of all the vehicles
    std::vector<std::string> vehicleIds = traci.vehicle.getIDList();

    // Get the positions of all the vehicles
    for (const std::string &id : vehicleIds)
    {
        double x = traci.vehicle.getPosition(id).x;
        double y = traci.vehicle.getPosition(id).y;
        double heading = traci.vehicle.getAngle(id);
        GeoCoordinates result = GeoConverter::convertGeo(x, y);
        QVariantMap vehicle;
        vehicle["id"] = QString::fromStdString(id);
        vehicle["latitude"] = result.lat;
        vehicle["longitude"] = result.lon;
        vehicle["rotation"] = heading;

        QColor carColor = applyColor(QString::fromStdString(id)); // Convertit la couleur en QVariant et l'associe à la clé "color" dans le QVariantMap
        QVariant colorVariant = QVariant::fromValue(carColor);
        vehicle["color"] = colorVariant;

        newPositions.append(vehicle);

        /*
        qDebug() << "Vehicle ID:" << QString::fromStdString(id)
                 << "Color:" << carColor
                 << "Color Variant:" << colorVariant;

        qDebug() << "Vehicle ID:" << QString::fromStdString(id)
                 << "Latitude:" << result.lat
                 << "Longitude:" << result.lon;

                 */
    }
    first_init = false;

    // Check if the positions have changed
    if (newPositions != vehiclePositions)
    {
        vehiclePositions = newPositions;
        emit vehiclePositionsChanged();
        emit vehiclePositionsUpdated(newPositions);
    }
}
