<?php

$servername = "localhost";
$username   = "root";
$password   = "";
$dbname     = "arthritis_patient";

$conn = new mysqli(
    $servername,
    $username,
    $password,
    $dbname
);

if ($conn->connect_error) {
    die("Database connection failed");
}

if (!isset($_GET['event'])) {
    die("ERROR: Event missing");
}

$event = strtoupper(trim($_GET['event']));
$state = isset($_GET['state']) ? intval($_GET['state']) : 1;

$state = ($state == 1) ? 1 : 0;

/* Map band gesture codes (and optional names) to readings row id=1. */
/* G1,G2,G7,G8 -> light | G3,G4,G9,G10 -> fan | G5,G6 -> bulb | G11-G15 -> alert */

switch ($event) {

    case "LIGHT":
    case "G1":
    case "G2":
    case "G7":
    case "G8":
        $sql = "UPDATE readings SET light = ? WHERE id = 1";
        break;

    case "FAN":
    case "G3":
    case "G4":
    case "G9":
    case "G10":
        $sql = "UPDATE readings SET fan = ? WHERE id = 1";
        break;

    case "BULB":
    case "G5":
    case "G6":
        $sql = "UPDATE readings SET bulb = ? WHERE id = 1";
        break;

    case "G11":
    case "G12":
    case "G13":
    case "G14":
    case "G15":
    case "ALERT":
        $sql = "UPDATE readings SET alert = ? WHERE id = 1";
        break;

    default:
        die("ERROR: Unknown event");
}

$stmt = $conn->prepare($sql);
$stmt->bind_param("i", $state);

if ($stmt->execute()) {
    echo "SUCCESS";
} else {
    echo "ERROR: Update failed";
}

$stmt->close();
$conn->close();

?>
