-- phpMyAdmin SQL Dump
-- version 4.1.6
-- http://www.phpmyadmin.net
--
-- Host: 127.0.0.1
-- Generation Time: Sep 30, 2026 at 07:32 AM
-- Server version: 5.6.16
-- PHP Version: 5.5.9

SET SQL_MODE = "NO_AUTO_VALUE_ON_ZERO";
SET time_zone = "+00:00";


/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8 */;

--
-- Database: `arthritis_patient`
--

-- If upgrading an existing DB, run once:
-- ALTER TABLE `readings` ADD `bulb` int(11) DEFAULT '0' AFTER `fan`;

-- --------------------------------------------------------

--
-- Table structure for table `readings`
--

CREATE TABLE IF NOT EXISTS `readings` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `light` int(11) DEFAULT '0',
  `fan` int(11) DEFAULT '0',
  `bulb` int(11) DEFAULT '0',
  `help` int(11) DEFAULT '0',
  `not_set` int(11) DEFAULT '0',
  `alert` int(11) DEFAULT '0',
  `event_type` varchar(20) NOT NULL,
  `timestamp` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB  DEFAULT CHARSET=latin1 AUTO_INCREMENT=16 ;

--
-- Dumping data for table `readings`
--

INSERT INTO `readings` (`id`, `light`, `fan`, `bulb`, `help`, `not_set`, `alert`, `event_type`, `timestamp`) VALUES
(1, 1, 0, 0, 0, 0, 0, 'LIGHT', '2026-09-29 10:25:31'),
(2, 1, 0, 0, 0, 0, 0, 'LIGHT', '2026-09-29 10:25:43'),
(3, 0, 1, 0, 0, 0, 0, 'FAN', '2026-09-29 10:25:50'),
(4, 0, 0, 0, 1, 0, 0, 'HELP', '2026-09-29 10:25:57'),
(5, 0, 0, 0, 0, 1, 0, 'NOT_SET', '2026-09-29 10:26:08'),
(6, 1, 0, 0, 0, 0, 0, 'LIGHT', '2026-09-29 10:39:39'),
(7, 1, 0, 0, 0, 0, 0, 'LIGHT', '2026-09-29 10:41:33'),
(8, 1, 0, 0, 0, 0, 0, 'LIGHT', '2026-09-29 10:41:39'),
(9, 0, 0, 0, 0, 1, 0, 'NOT_SET', '2026-09-29 10:41:44'),
(10, 1, 0, 0, 0, 0, 0, 'LIGHT', '2026-09-29 10:41:51'),
(11, 0, 1, 0, 0, 0, 0, 'FAN', '2026-09-29 10:41:57'),
(12, 0, 0, 0, 1, 0, 0, 'HELP', '2026-09-29 10:42:03'),
(13, 0, 0, 0, 0, 1, 0, 'NOT_SET', '2026-09-29 10:42:10'),
(14, 0, 1, 0, 0, 0, 0, 'FAN', '2026-09-29 10:42:17'),
(15, 1, 0, 0, 0, 0, 0, 'LIGHT', '2026-09-29 11:06:34');

/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
