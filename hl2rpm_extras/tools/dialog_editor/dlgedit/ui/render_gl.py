"""OpenGL 3.3 resources of the render window: shaders, textures of materials, the
world mesh, model meshes with instancing, and dynamic line/point buffers."""
import ctypes
import time

import numpy as np
import OpenGL

OpenGL.ERROR_CHECKING = False
OpenGL.ERROR_LOGGING = False
OpenGL.ERROR_ON_COPY = False
from OpenGL import GL  # noqa: E402

VS_MESH = """#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec2 aUV;
layout(location=3) in float aAlpha;
layout(location=4) in vec4 iC0;
layout(location=5) in vec4 iC1;
layout(location=6) in vec4 iC2;
layout(location=7) in vec4 iC3;
uniform mat4 uVP;
uniform mat4 uModel;
uniform int uInstanced;
out vec3 vN;
out vec2 vUV;
out float vA;
out vec3 vW;
void main() {
    mat4 M = (uInstanced == 1) ? mat4(iC0, iC1, iC2, iC3) : uModel;
    vec4 w = M * vec4(aPos, 1.0);
    vW = w.xyz;
    vN = mat3(M) * aNrm;
    vUV = aUV;
    vA = aAlpha;
    gl_Position = uVP * w;
}
"""

FS_MESH = """#version 330 core
in vec3 vN;
in vec2 vUV;
in float vA;
in vec3 vW;
uniform sampler2D uTex;
uniform sampler2D uTex2;
uniform int uHasTex;
uniform int uBlend;
uniform int uAlphaTest;
uniform vec4 uTint;
uniform vec3 uHighlight;
uniform vec3 uCam;
uniform float uFog;
uniform int uUnlit;
uniform vec3 uFogCol;
// lighting preview (M7): the sun of the hour, ambient, up to 32 lamps, cube shadows of 4
uniform int uLightMode;
uniform vec3 uSunDir;
uniform vec3 uSunCol;
uniform vec3 uAmbUp;
uniform vec3 uAmbDown;
uniform int uNumLights;
uniform vec4 uLPos[32];
uniform vec4 uLCol[32];
uniform vec4 uLSpot[32];
uniform float uLInner[32];
uniform int uLShadow[32];
uniform samplerCube uShadow0;
uniform samplerCube uShadow1;
uniform samplerCube uShadow2;
uniform samplerCube uShadow3;
out vec4 frag;
float shadowAt(int s, vec3 d) {
    float r;
    if (s == 0) r = texture(uShadow0, d).r;
    else if (s == 1) r = texture(uShadow1, d).r;
    else if (s == 2) r = texture(uShadow2, d).r;
    else r = texture(uShadow3, d).r;
    float dist = length(d);
    return (dist - 1.5 - dist * 0.012 > r) ? 0.0 : 1.0;
}
vec3 lighting(vec3 n, vec3 w) {
    vec3 acc = mix(uAmbDown, uAmbUp, 0.5 + 0.5 * n.z);
    acc += uSunCol * max(dot(n, uSunDir), 0.0);
    for (int i = 0; i < uNumLights; i++) {
        vec3 d = uLPos[i].xyz - w;
        float dist = length(d);
        if (dist > uLPos[i].w)
            continue;
        vec3 l = d / max(dist, 0.001);
        float nl = max(dot(n, l), 0.0);
        if (nl <= 0.0)
            continue;
        float att = pow(clamp(1.0 - dist / uLPos[i].w, 0.0, 1.0), uLCol[i].w);
        if (uLSpot[i].w > -1.5)
            att *= smoothstep(uLSpot[i].w, uLInner[i], dot(-l, uLSpot[i].xyz));
        if (uLShadow[i] >= 0 && att > 0.0)
            att *= shadowAt(uLShadow[i], -d);
        acc += uLCol[i].rgb * att * nl;
    }
    return acc;
}
void main() {
    vec4 c = (uHasTex == 1) ? texture(uTex, vUV) : vec4(0.62, 0.62, 0.62, 1.0);
    if (uBlend == 1) {
        vec4 c2 = texture(uTex2, vUV);
        c = vec4(mix(c.rgb, c2.rgb, clamp(vA, 0.0, 1.0)), 1.0);
    }
    if (uAlphaTest == 1 && c.a < 0.5)
        discard;
    vec3 n = normalize(vN);
    // seen from behind (double sided): towards the viewer. Not gl_FrontFacing - the winding of
    // Source faces is the opposite of GL's
    if (dot(n, uCam - vW) < 0.0)
        n = -n;
    vec3 L = normalize(vec3(0.45, 0.3, 0.85));
    float d = max(dot(n, L), 0.0);
    float sky = 0.5 + 0.5 * n.z;
    vec3 lit = (uUnlit == 1) ? c.rgb * uTint.rgb : c.rgb * uTint.rgb * (0.46 + 0.26 * sky + 0.55 * d);
    if (uLightMode == 1 && uUnlit == 0)
        lit = c.rgb * uTint.rgb * lighting(n, vW);
    lit += uHighlight;
    float dist = length(vW - uCam);
    float f = clamp((dist - uFog * 0.4) / uFog, 0.0, 0.55);
    lit = mix(lit, uFogCol, f);
    frag = vec4(lit, c.a * uTint.a);
}
"""

VS_LINE = """#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aCol;
uniform mat4 uVP;
uniform float uPointSize;
out vec4 vC;
void main() {
    vC = aCol;
    gl_Position = uVP * vec4(aPos, 1.0);
    gl_PointSize = uPointSize;
}
"""

FS_LINE = """#version 330 core
in vec4 vC;
out vec4 frag;
void main() { frag = vC; }
"""

# distance to a lamp into a cube map face (shadows of the lighting preview)
VS_DIST = """#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uVP;
out vec3 vW;
void main() { vW = aPos; gl_Position = uVP * vec4(aPos, 1.0); }
"""

FS_DIST = """#version 330 core
in vec3 vW;
uniform vec3 uLight;
out float frag;
void main() { frag = length(vW - uLight); }
"""

DXT_FORMATS = {"dxt1": 0x83F0, "dxt1a": 0x83F1, "dxt3": 0x83F2, "dxt5": 0x83F3}
GL_TEXTURE_MAX_ANISOTROPY = 0x84FE


def _compile(src, kind):
    sh = GL.glCreateShader(kind)
    GL.glShaderSource(sh, src)
    GL.glCompileShader(sh)
    if not GL.glGetShaderiv(sh, GL.GL_COMPILE_STATUS):
        raise RuntimeError(GL.glGetShaderInfoLog(sh).decode(errors="replace"))
    return sh


class Program:
    def __init__(self, vs, fs):
        self.id = GL.glCreateProgram()
        a = _compile(vs, GL.GL_VERTEX_SHADER)
        b = _compile(fs, GL.GL_FRAGMENT_SHADER)
        GL.glAttachShader(self.id, a)
        GL.glAttachShader(self.id, b)
        GL.glLinkProgram(self.id)
        if not GL.glGetProgramiv(self.id, GL.GL_LINK_STATUS):
            raise RuntimeError(GL.glGetProgramInfoLog(self.id).decode(errors="replace"))
        GL.glDeleteShader(a)
        GL.glDeleteShader(b)
        self.loc = {}

    def use(self):
        GL.glUseProgram(self.id)

    def u(self, name):
        l = self.loc.get(name)
        if l is None:
            l = self.loc[name] = GL.glGetUniformLocation(self.id, name)
        return l

    def mat4(self, name, m):
        GL.glUniformMatrix4fv(self.u(name), 1, GL.GL_TRUE, np.ascontiguousarray(m, dtype=np.float32))

    def i(self, name, v):
        GL.glUniform1i(self.u(name), v)

    def f(self, name, v):
        GL.glUniform1f(self.u(name), v)

    def v3(self, name, v):
        GL.glUniform3f(self.u(name), float(v[0]), float(v[1]), float(v[2]))

    def v4(self, name, v):
        GL.glUniform4f(self.u(name), float(v[0]), float(v[1]), float(v[2]), float(v[3]))


class GLMaterial:
    __slots__ = ("name", "tex", "tex2", "alphatest", "translucent", "additive", "tint", "water", "loaded")

    def __init__(self, name):
        self.name = name
        self.tex = 0
        self.tex2 = 0
        self.alphatest = False
        self.translucent = False
        self.additive = False
        self.tint = (1.0, 1.0, 1.0, 1.0)
        self.water = False
        self.loaded = False


class MaterialCache:
    """Materials -> GL textures; loaded a few per frame (the view draws gray until then)."""

    def __init__(self, matsys):
        self.ms = matsys
        self.mats = {}
        self.queue = []
        self.textures = {}       # texture name -> GL id (0 = missing)

    def get(self, name):
        m = self.mats.get(name)
        if m is None:
            m = self.mats[name] = GLMaterial(name)
            self.queue.append(m)
        return m

    def _texture(self, tex):
        if tex in self.textures:
            return self.textures[tex]
        t = self.ms.texture(tex)
        tid = upload_texture(t) if t is not None else 0
        self.textures[tex] = tid
        return tid

    def process(self, budget=0.03):
        """Load queued materials for up to `budget` seconds; returns how many are left."""
        t0 = time.perf_counter()
        while self.queue and time.perf_counter() - t0 < budget:
            m = self.queue.pop(0)
            info = self.ms.info(m.name)
            m.tex = self._texture(info.base) if info.base else 0
            m.tex2 = self._texture(info.base2) if info.base2 else 0
            m.alphatest = info.alphatest
            m.translucent = info.translucent and not info.alphatest
            m.additive = info.additive
            m.water = info.water
            if info.water:
                c = info.fogcolor
                m.tint = (c[0] * 1.6 + 0.08, c[1] * 1.6 + 0.1, c[2] * 1.6 + 0.12, 0.6)
                m.tex = 0
            else:
                m.tint = (info.tint[0], info.tint[1], info.tint[2], 0.55 if m.translucent else 1.0)
            m.loaded = True
        return len(self.queue)

    def delete(self):
        ids = [t for t in self.textures.values() if t]
        if ids:
            GL.glDeleteTextures(ids)
        self.textures = {}
        self.mats = {}
        self.queue = []


def upload_texture(t):
    tid = GL.glGenTextures(1)
    GL.glBindTexture(GL.GL_TEXTURE_2D, tid)
    if t.kind == "rgba":
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA8, t.width, t.height, 0, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE,
                        np.ascontiguousarray(t.data))
        GL.glGenerateMipmap(GL.GL_TEXTURE_2D)
    else:
        fmt = DXT_FORMATS[t.kind]
        n = 0
        for level, (w, h, blob) in enumerate(t.data):
            GL.glCompressedTexImage2D(GL.GL_TEXTURE_2D, level, fmt, w, h, 0, np.frombuffer(blob, dtype=np.uint8))
            n = level
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MAX_LEVEL, n)
    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MIN_FILTER, GL.GL_LINEAR_MIPMAP_LINEAR)
    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MAG_FILTER, GL.GL_LINEAR)
    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_WRAP_S, GL.GL_REPEAT)
    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_WRAP_T, GL.GL_REPEAT)
    try:
        GL.glTexParameterf(GL.GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0)
    except Exception:
        pass
    return tid


class WorldMesh:
    """All world batches in one buffer: interleaved pos3 nrm3 uv2 alpha1."""

    def __init__(self, batches):
        self.ranges = []        # (material name, first, count)
        arrays = []
        first = 0
        for name, b in sorted(batches.items()):
            n = len(b.pos)
            if n == 0:
                continue
            arrays.append(np.hstack([b.pos, b.nrm, b.uv, b.alpha[:, None]]).astype(np.float32))
            self.ranges.append((name, first, n))
            first += n
        self.count = first
        self.vao = GL.glGenVertexArrays(1)
        self.vbo = GL.glGenBuffers(1)
        GL.glBindVertexArray(self.vao)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self.vbo)
        data = np.ascontiguousarray(np.concatenate(arrays) if arrays else np.zeros((0, 9), np.float32))
        GL.glBufferData(GL.GL_ARRAY_BUFFER, data.nbytes, data, GL.GL_STATIC_DRAW)
        stride = 36
        for loc, size, off in ((0, 3, 0), (1, 3, 12), (2, 2, 24), (3, 1, 32)):
            GL.glEnableVertexAttribArray(loc)
            GL.glVertexAttribPointer(loc, size, GL.GL_FLOAT, False, stride, ctypes.c_void_p(off))
        GL.glBindVertexArray(0)

    def delete(self):
        GL.glDeleteVertexArrays(1, [self.vao])
        GL.glDeleteBuffers(1, [self.vbo])


class ModelMesh:
    """One studio model on the GPU (vertex + index buffers); VAOs per instance group."""

    def __init__(self, model):
        self.model = model
        self.parts = []          # (skinref, first index, count)
        verts, inds = [], []
        base = 0
        first = 0
        for m in model.meshes:
            verts.append(np.hstack([m.pos, m.nrm, m.uv]).astype(np.float32))
            inds.append(m.idx.astype(np.uint32) + base)
            self.parts.append((m.material, first, len(m.idx)))
            base += len(m.pos)
            first += len(m.idx)
        self.vbo = GL.glGenBuffers(1)
        self.ibo = GL.glGenBuffers(1)
        v = np.ascontiguousarray(np.concatenate(verts))
        i = np.ascontiguousarray(np.concatenate(inds))
        # the element buffer binding belongs to the bound VAO: a model met while drawing another one
        # would hand its indices to that model's VAO (its triangles then join random vertices)
        GL.glBindVertexArray(0)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self.vbo)
        GL.glBufferData(GL.GL_ARRAY_BUFFER, v.nbytes, v, GL.GL_STATIC_DRAW)
        GL.glBindBuffer(GL.GL_ELEMENT_ARRAY_BUFFER, self.ibo)
        GL.glBufferData(GL.GL_ELEMENT_ARRAY_BUFFER, i.nbytes, i, GL.GL_STATIC_DRAW)
        self.single_vao = self.make_vao(None)

    def make_vao(self, instance_vbo):
        vao = GL.glGenVertexArrays(1)
        GL.glBindVertexArray(vao)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self.vbo)
        for loc, size, off in ((0, 3, 0), (1, 3, 12), (2, 2, 24)):
            GL.glEnableVertexAttribArray(loc)
            GL.glVertexAttribPointer(loc, size, GL.GL_FLOAT, False, 32, ctypes.c_void_p(off))
        GL.glDisableVertexAttribArray(3)
        GL.glVertexAttrib1f(3, 1.0)
        if instance_vbo:
            GL.glBindBuffer(GL.GL_ARRAY_BUFFER, instance_vbo)
            for k in range(4):
                GL.glEnableVertexAttribArray(4 + k)
                GL.glVertexAttribPointer(4 + k, 4, GL.GL_FLOAT, False, 64, ctypes.c_void_p(16 * k))
                GL.glVertexAttribDivisor(4 + k, 1)
        GL.glBindBuffer(GL.GL_ELEMENT_ARRAY_BUFFER, self.ibo)
        GL.glBindVertexArray(0)
        return vao

    def delete(self):
        GL.glDeleteBuffers(2, [self.vbo, self.ibo])
        GL.glDeleteVertexArrays(1, [self.single_vao])


class ShadowCube:
    """A cube map of distances from a lamp to the world (R32F + its own depth buffer)."""
    FACES = (((1, 0, 0), (0, -1, 0)), ((-1, 0, 0), (0, -1, 0)), ((0, 1, 0), (0, 0, 1)),
             ((0, -1, 0), (0, 0, -1)), ((0, 0, 1), (0, -1, 0)), ((0, 0, -1), (0, -1, 0)))

    def __init__(self, size=512):
        self.size = size
        self.key = None
        self.tex = GL.glGenTextures(1)
        GL.glBindTexture(GL.GL_TEXTURE_CUBE_MAP, self.tex)
        for i in range(6):
            GL.glTexImage2D(GL.GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL.GL_R32F, size, size, 0, GL.GL_RED, GL.GL_FLOAT, None)
        for p, v in ((GL.GL_TEXTURE_MIN_FILTER, GL.GL_NEAREST), (GL.GL_TEXTURE_MAG_FILTER, GL.GL_NEAREST),
                     (GL.GL_TEXTURE_WRAP_S, GL.GL_CLAMP_TO_EDGE), (GL.GL_TEXTURE_WRAP_T, GL.GL_CLAMP_TO_EDGE),
                     (GL.GL_TEXTURE_WRAP_R, GL.GL_CLAMP_TO_EDGE)):
            GL.glTexParameteri(GL.GL_TEXTURE_CUBE_MAP, p, v)
        self.fbo = GL.glGenFramebuffers(1)
        self.depth = GL.glGenRenderbuffers(1)
        GL.glBindRenderbuffer(GL.GL_RENDERBUFFER, self.depth)
        GL.glRenderbufferStorage(GL.GL_RENDERBUFFER, GL.GL_DEPTH_COMPONENT24, size, size)

    @staticmethod
    def _look(eye, f, up):
        f = np.asarray(f, float)
        up = np.asarray(up, float)
        s = np.cross(f, up)
        u = np.cross(s, f)
        m = np.eye(4, dtype=np.float32)
        m[0, :3], m[1, :3], m[2, :3] = s, u, -f
        m[0, 3], m[1, 3], m[2, 3] = -s @ eye, -u @ eye, f @ eye
        return m

    def render(self, prog, light_pos, radius, draw_fn):
        """draw_fn(): draws the world (positions at location 0) with prog bound."""
        n, far = 1.0, max(radius, 8.0)
        proj = np.zeros((4, 4), dtype=np.float32)
        proj[0, 0] = proj[1, 1] = 1.0
        proj[2, 2] = -(far + n) / (far - n)
        proj[2, 3] = -2 * far * n / (far - n)
        proj[3, 2] = -1.0
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.fbo)
        GL.glFramebufferRenderbuffer(GL.GL_FRAMEBUFFER, GL.GL_DEPTH_ATTACHMENT, GL.GL_RENDERBUFFER, self.depth)
        GL.glViewport(0, 0, self.size, self.size)
        # the labels' QPainter of the previous frame leaves its state (depth writes off, scissor, stencil)
        GL.glDisable(GL.GL_SCISSOR_TEST)
        GL.glDisable(GL.GL_STENCIL_TEST)
        GL.glDisable(GL.GL_BLEND)
        GL.glColorMask(True, True, True, True)
        GL.glDepthMask(True)
        GL.glEnable(GL.GL_DEPTH_TEST)
        GL.glDepthFunc(GL.GL_LESS)
        prog.use()
        prog.v3("uLight", light_pos)
        eye = np.asarray(light_pos, float)
        for i, (f, up) in enumerate(self.FACES):
            GL.glFramebufferTexture2D(GL.GL_FRAMEBUFFER, GL.GL_COLOR_ATTACHMENT0, GL.GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, self.tex, 0)
            GL.glClearColor(1e9, 1e9, 1e9, 1.0)
            GL.glClear(GL.GL_COLOR_BUFFER_BIT | GL.GL_DEPTH_BUFFER_BIT)
            prog.mat4("uVP", proj @ self._look(eye, f, up))
            draw_fn()

    def delete(self):
        GL.glDeleteTextures([self.tex])
        GL.glDeleteFramebuffers(1, [self.fbo])
        GL.glDeleteRenderbuffers(1, [self.depth])


class SkinnedMesh:
    """An animated copy of a model mesh: its own dynamic vertex buffer (skinned positions and
    normals written every frame), the index buffer and parts of the static ModelMesh."""

    def __init__(self, mesh):
        self.mesh = mesh
        self.model = mesh.model
        self.parts = mesh.parts
        m = mesh.model
        self.uv = np.ascontiguousarray(np.concatenate([x.uv for x in m.meshes]), dtype=np.float32)
        self.pos = np.ascontiguousarray(np.concatenate([x.pos for x in m.meshes]), dtype=np.float32)
        self.nrm = np.ascontiguousarray(np.concatenate([x.nrm for x in m.meshes]), dtype=np.float32)
        bw = [x.bw if x.bw is not None else np.tile([1.0, 0, 0], (len(x.pos), 1)) for x in m.meshes]
        bb = [x.bb if x.bb is not None else np.zeros((len(x.pos), 3), np.int32) for x in m.meshes]
        self.bw = np.ascontiguousarray(np.concatenate(bw), dtype=np.float32)
        self.bb = np.ascontiguousarray(np.concatenate(bb), dtype=np.int32)
        n = len(self.uv)
        self.vbo = GL.glGenBuffers(1)
        GL.glBindVertexArray(0)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self.vbo)
        GL.glBufferData(GL.GL_ARRAY_BUFFER, n * 32, None, GL.GL_DYNAMIC_DRAW)
        self.vao = GL.glGenVertexArrays(1)
        GL.glBindVertexArray(self.vao)
        for loc, size, off in ((0, 3, 0), (1, 3, 12), (2, 2, 24)):
            GL.glEnableVertexAttribArray(loc)
            GL.glVertexAttribPointer(loc, size, GL.GL_FLOAT, False, 32, ctypes.c_void_p(off))
        GL.glDisableVertexAttribArray(3)
        GL.glVertexAttrib1f(3, 1.0)
        GL.glBindBuffer(GL.GL_ELEMENT_ARRAY_BUFFER, mesh.ibo)
        GL.glBindVertexArray(0)
        self.single_vao = self.vao          # drawn like a ModelMesh
        self.update(self.pos, self.nrm)

    def update(self, pos, nrm):
        data = np.ascontiguousarray(np.hstack([pos, nrm, self.uv]), dtype=np.float32)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self.vbo)
        GL.glBufferSubData(GL.GL_ARRAY_BUFFER, 0, data.nbytes, data)

    def delete(self):
        GL.glDeleteBuffers(1, [self.vbo])
        GL.glDeleteVertexArrays(1, [self.vao])


class InstanceGroup:
    def __init__(self, mesh, skin, matrices):
        self.mesh = mesh
        self.skin = skin
        self.count = len(matrices)
        data = np.ascontiguousarray(np.stack([m.T.reshape(16) for m in matrices]).astype(np.float32))
        self.vbo = GL.glGenBuffers(1)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self.vbo)
        GL.glBufferData(GL.GL_ARRAY_BUFFER, data.nbytes, data, GL.GL_STATIC_DRAW)
        self.vao = mesh.make_vao(self.vbo)
        centers = np.array([m[:3, 3] for m in matrices])
        self.center = centers.mean(0)
        self.radius = float(np.linalg.norm(centers - self.center, axis=1).max()) + 256.0

    def delete(self):
        GL.glDeleteBuffers(1, [self.vbo])
        GL.glDeleteVertexArrays(1, [self.vao])


class LineBuffer:
    """Dynamic colored lines / triangles / points (rebuilt when the overlay changes)."""

    def __init__(self):
        self.vao = GL.glGenVertexArrays(1)
        self.vbo = GL.glGenBuffers(1)
        GL.glBindVertexArray(self.vao)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self.vbo)
        GL.glEnableVertexAttribArray(0)
        GL.glVertexAttribPointer(0, 3, GL.GL_FLOAT, False, 28, ctypes.c_void_p(0))
        GL.glEnableVertexAttribArray(1)
        GL.glVertexAttribPointer(1, 4, GL.GL_FLOAT, False, 28, ctypes.c_void_p(12))
        GL.glBindVertexArray(0)
        self.count = 0

    def upload(self, data):
        data = np.ascontiguousarray(data, dtype=np.float32).reshape(-1, 7)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self.vbo)
        GL.glBufferData(GL.GL_ARRAY_BUFFER, data.nbytes, data if len(data) else None, GL.GL_DYNAMIC_DRAW)
        self.count = len(data)

    def draw(self, mode, first=0, count=None):
        n = self.count - first if count is None else count
        if n <= 0:
            return
        GL.glBindVertexArray(self.vao)
        GL.glDrawArrays(mode, first, n)

    def delete(self):
        GL.glDeleteBuffers(1, [self.vbo])
        GL.glDeleteVertexArrays(1, [self.vao])
