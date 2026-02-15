import 'dart:convert';
import 'dart:io';
import 'dart:async';
import 'package:flutter/material.dart';

HttpServer? server;
String lightIp = 'Initializing...';

// The Govee UDP port
const int portSend = 4001;
const int portReceive = 4002;

String trackevent = 'events.weather_change';
String trackstatus = 'dry';

void main() async {
  // Initialize Govee before starting the server or UI
  lightIp = await initializeGovee();

// Replace with your actual IP address
  const String serverIp = '192.168.68.65';

// Bind the server to the specified IP address
  server = await HttpServer.bind(InternetAddress(serverIp), 8080);
  print('Server running on http://$serverIp:8080/');

  server?.listen((HttpRequest request) async {
    // Allow cross-origin requests
    request.response.headers
        .add('Access-Control-Allow-Origin', '*'); // Allow all origins
    request.response.headers
        .add('Access-Control-Allow-Methods', 'POST'); // Allow POST requests
    request.response.headers.add('Access-Control-Allow-Headers',
        'Content-Type'); // Allow headers like Content-Type
    if (request.method == 'OPTIONS') {
      // Respond with status 200 for OPTIONS requests
      request.response
        ..statusCode = HttpStatus.ok
        ..write('');
      await request.response.close();
      return;
    }

    if (request.method == 'POST' &&
        request.headers.contentType?.mimeType == 'application/json') {
      try {
        // Parse the incoming JSON
        final content = await utf8.decoder.bind(request).join();
        final jsonData = jsonDecode(content) as Map<String, dynamic>;
        print('Received event data: $jsonData');

        final eventType = jsonData['event_type'];
        final eventData = jsonData['event_data'];

        // Handle the event based on its type
        await handleEvent(eventType, eventData);

        // Respond to the client
        request.response
          ..statusCode = HttpStatus.ok
          ..headers.contentType = ContentType.json
          ..write(jsonEncode({
            'status': 'success',
            'message': 'Event handled',
          }));
      } catch (e) {
        // Handle errors
        request.response
          ..statusCode = HttpStatus.internalServerError
          ..write('Error: $e');
      }
    } else {
      // Handle unsupported methods
      request.response
        ..statusCode = HttpStatus.methodNotAllowed
        ..write('Only POST requests are supported');
    }
    await request.response.close();
  });

  // Start the Flutter app UI
  runApp(MyApp());
}

// Function to initialize Govee and get light's IP address
Future<String> initializeGovee() async {
  InternetAddress multicastAddress = InternetAddress('239.255.255.250');
  const portSend = 4001;
  const portReceive = 4002;

  final command = jsonEncode({
    "msg": {
      "cmd": "scan",
      "data": {"account_topic": "reserve"}
    }
  });

  try {
    // Create a UDP socket to send the scan command
    final sendSocket = await RawDatagramSocket.bind(InternetAddress.anyIPv4, 0);
    print('Sending scan request to Govee lights...');
    sendSocket.send(utf8.encode(command), multicastAddress, portSend);
    sendSocket.close();

    // Create a UDP socket to listen for responses
    final receiveSocket =
        await RawDatagramSocket.bind(InternetAddress.anyIPv4, portReceive);
    print('Listening for responses on port $portReceive...');

    final completer = Completer<String>();

    receiveSocket.listen((RawSocketEvent event) {
      if (event == RawSocketEvent.read) {
        final datagram = receiveSocket.receive();
        if (datagram != null) {
          final response = utf8.decode(datagram.data);
          print('Received response: $response');

          // Parse the JSON response
          final Map<String, dynamic> responseData = jsonDecode(response);

          // Navigate the nested structure to find the IP address
          final ip = responseData['msg']?['data']?['ip'];

          if (ip != null) {
            print('Found light IP: $ip');
            completer.complete(ip);
            receiveSocket.close();
          } else {
            print('IP address not found in response');
          }
        }
      }
    });

    return completer.future;
  } catch (e) {
    print('Error initializing Govee: $e');
    return 'Failed to initialize';
  }
}

// Function to handle the event and send commands to Govee
Future<void> handleEvent(String eventType, dynamic eventData) async {
  if (lightIp == 'Initializing...') {
    print('Govee light not initialized yet.');
    return;
  }

  switch (eventType) {
    case 'events.weather_update':
      if (eventData == 'about_to_rain') {
        print('The weather is about to rain. Taking action...');
        trackevent = eventType;
        trackstatus = eventData;
        await sendGoveeColorCommand(255, 0, 255); // Purple
      } else if (eventData == 'about_to_dry_up') {
        print('The weather is about to dry up. Taking action...');
        trackevent = eventType;
        trackstatus = eventData;
        await sendGoveeColorCommand(0, 255, 0); // Green
      } else {
        print('Unknown weather update: $eventData');
      }
      break;

    case 'events.weather_change':
      if (eventData == 'wet') {
        print('Weather is wet. Taking action...');
        trackevent = eventType;
        trackstatus = eventData;
        await sendGoveeColorCommand(0, 10, 255); // Blue
      } else if (eventData == 'dry') {
        print('Weather is dry. Taking action...');
        trackevent = eventType;
        trackstatus = eventData;
        await sendGoveeColorCommand(255, 255, 255); // White
      } else {
        print('Unknown weather change: $eventData');
      }
      break;

    case 'event.change_status':
      if (eventData['new'] == 'suspended') {
        print('Status changed to suspended. Taking action...');
        await sendGoveeColorCommand(255, 0, 0); // Red
      } else if (eventData['new'] == 'running' || eventData['new'] == 'restarting') {
        print('Status changed to running/restarting. Checking previous state...');

        // Ensure `trackevent` and `trackstatus` are valid
        if (trackevent.isNotEmpty && (trackstatus == 'about_to_rain' || trackstatus == 'about_to_dry_up' || trackstatus == 'wet' || trackstatus == 'dry')) {
          print('Re-handling previous state: event=$trackevent, status=$trackstatus');
          await handleEvent(trackevent, trackstatus);
        } else {
          print('No valid previous state to re-handle. Skipping...');
        }
      } else {
        print('Unknown status change: ${eventData['new']}');
      }
      break;

    default:
      print('Unhandled event type: $eventType');
      break;
  }
}

// Example function to handle "ui.lap_update" event
Future<void> handleLapUpdate(dynamic eventData) async {
  if (eventData == null) {
    print('No event data provided for lap_update');
    return;
  }

  // Example of handling a brightness change (as an event data parameter)
  if (eventData['brightness'] != null) {
    int brightness = eventData['brightness'];
    print('Setting brightness to: $brightness');
    // await sendGoveeCommand('brightness', brightness);
  }

  // Handle more event data fields as needed (e.g., color, effect, etc.)
}

Future<void> handleUpdateWeather(dynamic eventData) async {}

// Function to send UDP command to Govee light
Future<void> sendGoveeColorCommand(int red, int green, int blue) async {
  if (lightIp == 'Initializing...') {
    print('Govee light IP not initialized yet.');
    return;
  }

  // Prepare the command data
  final command = jsonEncode({
    "msg": {
      "cmd": "colorwc",
      "data": {
        "color": {"r": red, "g": green, "b": blue},
        "colorTemInKelvin": 0
      }
    }
  });

  // Create a UDP socket to send the command
  final sendSocket = await RawDatagramSocket.bind(InternetAddress.anyIPv4, 0);

  // Send the UDP command to the light's IP
  sendSocket.send(utf8.encode(command), InternetAddress(lightIp), portSend);
  sendSocket.close();
  print('Sent command to Govee light: $command');
}

// Function to send UDP command to Govee light
Future<void> sendGoveeCommand(String cmd, int value) async {
  if (lightIp == 'Initializing...') {
    print('Govee light IP not initialized yet.');
    return;
  }

  // Prepare the command data
  final command = jsonEncode({
    "msg": {
      "cmd": cmd,
      "data": {
        "value": value,
      }
    }
  });

  // Create a UDP socket to send the command
  final sendSocket = await RawDatagramSocket.bind(InternetAddress.anyIPv4, 0);

  // Send the UDP command to the light's IP
  sendSocket.send(utf8.encode(command), InternetAddress(lightIp), portSend);
  sendSocket.close();
  print('Sent command to Govee light: $command');
}

class MyApp extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Server and Govee Initialization',
      home: Scaffold(
        appBar: AppBar(title: Text('Govee Light Control Server')),
        body: Center(
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Text('Govee Light IP:'),
              SizedBox(height: 20),
              Text(
                lightIp,
                style: TextStyle(fontSize: 24, fontWeight: FontWeight.bold),
              ),
              SizedBox(height: 30),
              ElevatedButton(
                onPressed: () {
                  stopServer();
                },
                child: Text('Stop Server'),
              ),
              SizedBox(height: 20),
              ElevatedButton(
                onPressed: () async {
                  await handleEvent('events.weather_update', 'about_to_rain');
                },
                child: Text('About to Rain'),
              ),
              ElevatedButton(
                onPressed: () async {
                  await handleEvent('events.weather_update', 'about_to_dry_up');
                },
                child: Text('About to Dry Up'),
              ),
              ElevatedButton(
                onPressed: () async {
                  await handleEvent('events.weather_change', 'wet');
                },
                child: Text('Weather Wet'),
              ),
              ElevatedButton(
                onPressed: () async {
                  await handleEvent('events.weather_change', 'dry');
                },
                child: Text('Weather Dry'),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

// Function to stop the server
void stopServer() {
  server?.close(force: true).then((_) {
    print('Server stopped.');
  }).catchError((e) {
    print('Error stopping the server: $e');
  });
}
