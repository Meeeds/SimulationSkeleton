#include <SFML/Graphics.hpp>
#include <vector>
#include <thread>
#include <random>
#include <cmath>
#include <iostream>
#include <cassert>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


static int K_PARTICLES = 8;
static int WORLD_WIDTH = 1920;
static int WORLD_HEIGTH = 1080;
static int DOT_SIZE = 2;
static int K_NB_TYPE = 16;
static sf::Vector2f DOT_OFSET = sf::Vector2f(DOT_SIZE, DOT_SIZE);

static bool g_centerize = true;

// Simple struct to hold particle data
struct Particle {
    sf::Vector2f position;
    sf::Vector2f velocity;
    float orientation;
    float angularVelocity;
    sf::Vector2f force;
    float torque;
    int type;
    //std::vector<Particle*> linked;
    //Having view relating object in the model object is not ideal
    //but in SFML not rebuilding the graphical object is significantly faster
    sf::CircleShape shape;
};

//////////////////////////////////////////////////////////////////////////////
float angleFromVector(sf::Vector2f v) {
    return std::atan2(v.y, v.x);
}

//////////////////////////////////////////////////////////////////////////////
sf::Vector2f unitVectorFromAngle(float iAngle) {
    return sf::Vector2f(std::cos(iAngle), std::sin(iAngle));
}

//////////////////////////////////////////////////////////////////////////////
sf::Vector2f rotateVector(const sf::Vector2f& v, float alpha) {
    float cs = std::cos(alpha);
    float sn = std::sin(alpha);

    sf::Vector2f rotatedVector;
    rotatedVector.x = v.x * cs - v.y * sn;
    rotatedVector.y = v.x * sn + v.y * cs;
    return rotatedVector;
}

/*
float colinearFactor(sf::Vector2f v1, sf::Vector2f v2) {
    float dotProduct = v1.x * v2.x + v1.y * v2.y;
    float lengthsProduct = std::sqrt(v1.x * v1.x + v1.y * v1.y) * std::sqrt(v2.x * v2.x + v2.y * v2.y);

    // prevent division by zero
    if(lengthsProduct == 0.f)
        return 0.f;

    float cosineOfAngle = dotProduct / lengthsProduct;

    // Clamp the value to the [-1, 1] range, in case of numerical instability
    cosineOfAngle = std::max(-1.f, std::min(1.f, cosineOfAngle));

    return cosineOfAngle;
}
*/

//////////////////////////////////////////////////////////////////////////////
float norm(const sf::Vector2f& vec) {
    return std::sqrt(vec.x * vec.x + vec.y * vec.y);
}

//////////////////////////////////////////////////////////////////////////////
void normalize(sf::Vector2f& vec)
{
    float normVec = norm(vec);
    if (!normVec == 0) vec /= normVec;
}

//////////////////////////////////////////////////////////////////////////////
sf::Color getColor(int type) {
    // Convert the type to a hue value between 0 and 360 degrees
    float hue = (type % K_NB_TYPE) * (360.0f / 16.0f);

    // For simplicity, we'll keep saturation and lightness constant
    float saturation = 1.0f;
    float lightness = 0.5f;

    // Convert from HSL to RGB using the formula
    float c = (1.0f - std::abs(2.0f * lightness - 1.0f)) * saturation;
    float x = c * (1.0f - std::abs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
    float m = lightness - c / 2.0f;

    float r = 0, g = 0, b = 0;
    if (0 <= hue && hue < 60) {
        r = c, g = x, b = 0;
    } else if (60 <= hue && hue < 120) {
        r = x, g = c, b = 0;
    } else if (120 <= hue && hue < 180) {
        r = 0, g = c, b = x;
    } else if (180 <= hue && hue < 240) {
        r = 0, g = x, b = c;
    } else if (240 <= hue && hue < 300) {
        r = x, g = 0, b = c;
    } else if (300 <= hue && hue < 360) {
        r = c, g = 0, b = x;
    }

    return sf::Color((r + m) * 255, (g + m) * 255, (b + m) * 255);
}

struct Model {
    std::vector<Particle> particles; //BAD choice of container if dynamic
    std::random_device rd;
    std::mt19937 gen;

    Model() : gen(rd())
    {
        init();
    }

    void init()
    {
        // Initialize random number generator
        std::uniform_int_distribution<> disType(0, K_NB_TYPE-1);
        std::uniform_real_distribution<> disX(-WORLD_WIDTH/16, WORLD_WIDTH/16);
        std::uniform_real_distribution<> disY(-WORLD_HEIGTH/16, WORLD_HEIGTH/16);
        std::uniform_real_distribution<> disV(-2, 2);
        std::uniform_real_distribution<> disA(0, 2.0*M_PI);

        // Initialize particles
        particles.clear();
        particles.reserve(K_PARTICLES); //BAD choice of container if dynamic
        for (int i = 0; i < K_PARTICLES; ++i) {
            Particle p;
            p.position = sf::Vector2f(disX(gen), disY(gen));
            p.velocity = sf::Vector2f(disV(gen), disV(gen));
            p.orientation = disA(gen);
            p.angularVelocity = 0.0;
            p.type = disType(gen);
            p.shape = sf::CircleShape(DOT_SIZE);
            p.shape.setOrigin(DOT_SIZE, DOT_SIZE);
            p.shape.setPosition(p.position);
            particles.push_back(p);
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    void calculateForceAndTorque_polar1(const sf::Vector2f& pos1,
                                        const sf::Vector2f& pos2,
                                        float orientation1,
                                        float orientation2,
                                        const sf::Vector2f& r,
                                        float rNorm,
                                        sf::Vector2f& oForce,
                                        float& oTorque)
    {
        // The interaction strength
        float strengthF = 1.0f;
        float strengthT = 1.0f;
        float divAngle = 0.0f;
        float teta = orientation2 - orientation1;
        // Make sure it is between -pi and pi
        while (teta > M_PI) teta -= 2 * M_PI;
        while (teta < -M_PI) teta += 2 * M_PI;
        float phi = angleFromVector(r) - orientation1;
        // Make sure it is between -pi and pi
        while (phi > M_PI) phi -= 2 * M_PI;
        while (phi < -M_PI) phi += 2 * M_PI;

        float phipi = phi;
        while (phipi > M_PI/2.0) phipi -= M_PI;
        while (phipi < -M_PI/2.0) phipi += M_PI;

        //Here I need a f such that:
        //f(teta, phi) = f(-teta, phi-teta+pi) //action reaction symmetry)
        //f(teta, phi) = f(-teta, -phi) //mirror symmetry
        float anisoFactor = 1.0f;//sin(teta)*sin(phi)+sin(teta)*sin(phi-teta) + 1.6f;
        //Maybe instead of rotating we can add an orthogonal force (maybe not needed)
        //2*phi-teta is an invariant angle in an interacting pair
        float rotatorFactor = sin(2*phi-teta+M_PI);//(sin(teta)*sin(phi)+sin(teta)*sin(phi-teta))/2.0f;// * M_PI;
        float distanceFactor = 1.0f / std::pow(rNorm, 2);

        oForce = rotateVector(r, rotatorFactor) * (10.0f*rotatorFactor*rotatorFactor + 1.0f) * strengthF * anisoFactor * distanceFactor;

        float dirFactor = ((teta>0) && (teta<divAngle) || (teta<0) && (teta<-divAngle)) ? -1.0f : 1.0f;
        //float dirFactor = (teta<0) ? -1.0f : 1.0f;
        oTorque = dirFactor*strengthT;

        //Torque influence as well diminish with distance
        oTorque = oTorque/(rNorm);
    }

    //////////////////////////////////////////////////////////////////////////////
    void calculateForce_linearAttraction(float forceMagnitude, const sf::Vector2f& r, float rNorm, sf::Vector2f& force) {
        force = r * (forceMagnitude / rNorm);
    }

    //////////////////////////////////////////////////////////////////////////////
    void calculateForce_quadraticAttraction(float forceMagnitude, const sf::Vector2f& r, float rNorm, sf::Vector2f& force) {
        force = r * (forceMagnitude / (rNorm*rNorm));
    }

    //////////////////////////////////////////////////////////////////////////////
    void step()
    {
        const float interactionRadius = 20.0f;

        // Calculate the force and torque on particle p due to all other particles
        for (auto& p : particles) {

            p.force = sf::Vector2f(0.0, 0.0);
            p.torque = 0.0;

            //General forces with any other particles
            for (Particle& other : particles) {
                if (&other != &p) {  // Avoid self-interaction
                    sf::Vector2f r = other.position - p.position;
                    float rNorm = norm(r);
                    if (rNorm < interactionRadius) {  // Consider only particles within the interaction radius
                        // Calculate force and torque using appropriate model
                        sf::Vector2f force(0.0, 0.0);
                        float torque = 0.0;
                        //Force model for vesicle formation
                        calculateForceAndTorque_polar1(p.position, other.position,
                                                       p.orientation, other.orientation,
                                                       r, rNorm,
                                                       force, torque);

                        p.force += force;
                        p.torque += torque;

                        //Solid repulsion
                        if (rNorm < 2.0*2.0*DOT_SIZE) //The first 2 is for progressive smoothing
                        {
                            float factor = std::pow(2.0*DOT_SIZE/rNorm, 9);
                            p.force += -r * factor;
                        }
                    }
                }
                // Floc behavior
                //p.velocity = multiply(p.velocity, 0.999);
                //p.velocity += multiply(other.velocity, 0.0001);
                //p.angularVelocity = p.angularVelocity * 0.999;
                //p.angularVelocity += other.angularVelocity * 0.0001;
            }

            //Link forces
            /*assert(p.linked.size() <= 1);
            for (Particle* otherLinked : p.linked) {
                if (otherLinked != &p) {  // Avoid self-interaction
                    sf::Vector2f r = otherLinked->position - p.position;
                    float rNorm = norm(r);
                    // Calculate force and torque using appropriate model
                    sf::Vector2f force(0.0, 0.0);
                    float torque = 0.0;
                    //Force model for vesicle formation
                    calculateForce_linearAttraction(20.0f, r, rNorm, force);
                    p.force += force;
                    p.torque += torque;
                }
            }*/

            // Containing forces
            if (p.position.x > WORLD_WIDTH/2) p.force += sf::Vector2f(-0.1,0.0);
            if (p.position.x < -WORLD_WIDTH/2) p.force += sf::Vector2f(0.1,0.0);
            if (p.position.y > WORLD_HEIGTH/2) p.force += sf::Vector2f(0.0,-0.1);
            if (p.position.y < -WORLD_HEIGTH/2) p.force += sf::Vector2f(0.0,0.1);
        }

        for (auto& p : particles) {
            // Calculate the acceleration and angular acceleration (assuming mass and moment of inertia = 1)
            sf::Vector2f acceleration = p.force;
            float angularAcceleration = p.torque;
            float dt = 0.01;

            // Using naive algo
            p.velocity = p.velocity + acceleration * dt;
            p.angularVelocity = p.angularVelocity + angularAcceleration * dt;
            float maxVelocity = 50.0;
            if (norm(p.velocity) > maxVelocity) {
                normalize(p.velocity);
                p.velocity *= maxVelocity;
            }
            //Force attract to center to incentive interactions
            if (g_centerize) {
                p.velocity -= 0.001f * p.position;
            }
            //Sticky dissipative space and other limits
            p.velocity = 0.999f * p.velocity;
            p.angularVelocity *= 0.999f;
            float maxAngularVelocity = 10.0;
            if (p.angularVelocity > maxAngularVelocity) p.angularVelocity = maxAngularVelocity;
            if (p.angularVelocity < -maxAngularVelocity) p.angularVelocity = -maxAngularVelocity;
            p.position = p.position + p.velocity * dt;
            p.orientation = p.orientation + p.angularVelocity * dt;

            // Update the particle's shape position
            p.shape.setPosition(p.position);
            p.shape.setFillColor(getColor(p.type));
        }
    }
};

void drawModel(sf::RenderWindow& ioWindow, const Model& iModel) {
    for (const auto& p : iModel.particles)
    {
        //sf::VertexArray lines(sf::Lines, 2);
        //for (auto& other : p.linked) {
        //    lines[0].position = p.position;
        //    lines[1].position = other->position;
        //    lines[0].color = sf::Color::Green;
        //    lines[1].color = sf::Color::Green;
            //ioWindow.draw(lines);
        //}

        float tailLength = 10.0;
        sf::Vertex line[] =
        {
            sf::Vertex(p.position, sf::Color::White),
            sf::Vertex(p.position + tailLength*unitVectorFromAngle(p.orientation), sf::Color::White)
        };
        ioWindow.draw(line, 2, sf::Lines);

        //Force debug
        sf::Vertex lineF[] =
        {
            sf::Vertex(p.position, sf::Color::Red),
            sf::Vertex(p.position + 10.0f*p.force, sf::Color::Red)
        };
        //ioWindow.draw(lineF, 2, sf::Lines);


        //Speed debug
        //sf::Vertex lineV[] =
        //{
        //    sf::Vertex(p.position, sf::Color::Green),
        //    sf::Vertex(p.position + 10.0f*p.velocity, sf::Color::Green)
        //};
        //ioWindow.draw(lineV, 2, sf::Lines);

        ioWindow.draw(p.shape);
        ioWindow.draw(lineF, 2, sf::Lines);
        //ioWindow.draw(lines);

        //Pole debug
        /*
        float teta = 1.5;
        sf::Vector2f aRVect = unitVectorFromAngle(p.orientation+teta/2.0+M_PI/2.0);
        sf::Vector2f aRPole = p.position + 5.0f * aRVect;
        sf::Vector2f aLVect = unitVectorFromAngle(p.orientation-teta/2.0-M_PI/2.0);
        sf::Vector2f aLPole = p.position + 5.0f * aLVect;
        sf::CircleShape aRPoleDot(1);
        aRPoleDot.setOrigin(1, 1);
        aRPoleDot.setPosition(aRPole);
        sf::CircleShape aLPoleDot(1);
        aLPoleDot.setOrigin(1, 1);
        aLPoleDot.setPosition(aLPole);
        aRPoleDot.setFillColor(sf::Color::Red);
        aLPoleDot.setFillColor(sf::Color::Red);
        ioWindow.draw(aRPoleDot);
        ioWindow.draw(aLPoleDot);
        */
    }
}

// Main function
int main()
{
    // Test
    //std::cout << angleFromVector(sf::Vector2f(-1.0,-2.0)) << std::endl;

    // Create the main window
    sf::RenderWindow window(sf::VideoMode(WORLD_WIDTH, WORLD_HEIGTH), "Particle system");

    // Model
    Model myModel;

    // Create a view with the same size as the window
    sf::View view(sf::FloatRect(-WORLD_WIDTH/2, -WORLD_HEIGTH/2, WORLD_WIDTH, WORLD_HEIGTH));

    // Variables to store the state of mouse dragging
    bool isDragging = false;
    sf::Vector2f lastMousePos;

    // main loop
    while (window.isOpen()) {
        // Handle events
        sf::Event event;
        while (window.pollEvent(event)) {
            switch (event.type)
            {
            case sf::Event::Closed:
                window.close();
                break;
            case sf::Event::MouseWheelScrolled:
                // If the wheel scrolled up, zoom in, else zoom out
                if (event.mouseWheelScroll.delta > 0)
                    view.zoom(0.9f);
                else
                    view.zoom(1.1f);
                break;
            case sf::Event::MouseButtonPressed:
                if (event.mouseButton.button == sf::Mouse::Left) {
                    isDragging = true;
                    lastMousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));
                }
                break;
            case sf::Event::MouseButtonReleased:
                if (event.mouseButton.button == sf::Mouse::Left)
                    isDragging = false;
                break;
            case sf::Event::KeyPressed:
                if (event.key.code == sf::Keyboard::C)
                    g_centerize = !g_centerize;
                break;
            }
        }

        // Check for mouse dragging
        if (isDragging && sf::Mouse::isButtonPressed(sf::Mouse::Left)) {
            const sf::Vector2f mousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));
            const sf::Vector2f delta = lastMousePos - mousePos;

            view.move(delta);
        }

        // Refresh
        window.setView(view);

        // Update the last mouse position
        lastMousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));

        // Model update
        myModel.step();

        // Clear screen
        window.clear();

        // Draw particles
        drawModel(window, myModel);

        // Update the window
        window.display();
    }

    return 0;
}
